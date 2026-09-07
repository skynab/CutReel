#include <cmath>
#include <cstdint>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "zaro/core/edit/Operations.h"
#include "zaro/core/render/RenderGraph.h"
#include "zaro/core/render/TransitionShape.h"

#include "ModelFixtures.h"
#include "TestSources.h"

using namespace zaro;
using Catch::Approx;
using zaro::testing::Fixture;
using zaro::testing::SolidFrameSource;

namespace {

model::Transition transitionOf(model::TransitionKind kind, model::TransitionDirection direction) {
    model::Transition out;
    out.kind = kind;
    out.direction = direction;
    return out;
}

}  // namespace

TEST_CASE("A dissolve is opacity and nothing else", "[render][transition]") {
    const auto shape = render::transitionShapeFor(
        transitionOf(model::TransitionKind::CrossDissolve, model::TransitionDirection::Right), 0.25,
        1920, 1080);
    CHECK(shape.incoming.opacity == Approx(0.25));
    CHECK(shape.incoming.offsetX == Approx(0.0));
    CHECK_FALSE(shape.incoming.mask.isSet());
}

TEST_CASE("A wipe uncovers from the side it travels from", "[render][transition]") {
    // Right means the edge travels right, so it uncovers from the left. The
    // same word means the same thing for a slide, which is why they share it.
    const auto shape = render::transitionShapeFor(
        transitionOf(model::TransitionKind::Wipe, model::TransitionDirection::Right), 0.25, 1000,
        500);
    REQUIRE(shape.incoming.mask.isSet());
    CHECK(shape.incoming.opacity == Approx(1.0));
    CHECK(shape.incoming.mask.width == Approx(250.0));
    CHECK(shape.incoming.mask.height == Approx(500.0));
    // Centred in the quarter it covers: 125 from the left edge, which is -375
    // from the centre of a 1000-wide frame.
    CHECK(shape.incoming.mask.centreX == Approx(-375.0));

    SECTION("and the other way round from the other side") {
        const auto other = render::transitionShapeFor(
            transitionOf(model::TransitionKind::Wipe, model::TransitionDirection::Left), 0.25, 1000,
            500);
        CHECK(other.incoming.mask.centreX == Approx(375.0));
    }

    SECTION("and vertically for up and down") {
        const auto down = render::transitionShapeFor(
            transitionOf(model::TransitionKind::Wipe, model::TransitionDirection::Down), 0.5, 1000,
            500);
        CHECK(down.incoming.mask.height == Approx(250.0));
        CHECK(down.incoming.mask.centreY == Approx(-125.0));
        CHECK(down.incoming.mask.width == Approx(1000.0));
    }
}

TEST_CASE("A wipe covers everything at the end and nothing at the start", "[render][transition]") {
    const auto transition =
        transitionOf(model::TransitionKind::Wipe, model::TransitionDirection::Right);
    const auto start = render::transitionShapeFor(transition, 0.0, 800, 600);
    CHECK(start.incoming.mask.width == Approx(0.0));
    const auto end = render::transitionShapeFor(transition, 1.0, 800, 600);
    CHECK(end.incoming.mask.width == Approx(800.0));
    CHECK(end.incoming.mask.centreX == Approx(0.0));
}

TEST_CASE("A slide starts off screen and ends home", "[render][transition]") {
    const auto transition =
        transitionOf(model::TransitionKind::Slide, model::TransitionDirection::Right);
    const auto start = render::transitionShapeFor(transition, 0.0, 1000, 500);
    CHECK(start.incoming.offsetX == Approx(-1000.0));
    CHECK(start.incoming.opacity == Approx(1.0));
    CHECK_FALSE(start.incoming.mask.isSet());

    const auto half = render::transitionShapeFor(transition, 0.5, 1000, 500);
    CHECK(half.incoming.offsetX == Approx(-500.0));

    const auto end = render::transitionShapeFor(transition, 1.0, 1000, 500);
    CHECK(end.incoming.offsetX == Approx(0.0));

    SECTION("and travels the other way when told to") {
        const auto left = render::transitionShapeFor(
            transitionOf(model::TransitionKind::Slide, model::TransitionDirection::Left), 0.5, 1000,
            500);
        CHECK(left.incoming.offsetX == Approx(500.0));
        const auto down = render::transitionShapeFor(
            transitionOf(model::TransitionKind::Slide, model::TransitionDirection::Down), 0.5, 1000,
            500);
        CHECK(down.incoming.offsetY == Approx(-250.0));
        CHECK(down.incoming.offsetX == Approx(0.0));
    }
}

TEST_CASE("A wipe reaches the picture", "[render][transition]") {
    Fixture f;
    f.sequence().setSize(32, 16);
    SolidFrameSource source{32, 16};
    const model::MediaRefId second = f.addMedia("second.mov", 10000);
    source.define(f.longMedia, render::Rgba{1.0F, 0.0F, 0.0F, 1.0F});
    source.define(second, render::Rgba{0.0F, 0.0F, 1.0F, 1.0F});
    render::RenderGraph graph{source};

    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(50, 50, 500, second))));
    const model::ClipId firstId = f.track(f.v1).clips().front().id;
    const model::ClipId secondId = f.track(f.v1).clips().back().id;
    static_cast<void>(firstId);
    static_cast<void>(secondId);
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(50), f.at(20))));

    // Made a wipe the way somebody would: drop a dissolve on the cut, then
    // decide it wants to be something else.
    const model::TransitionId transitionId = f.track(f.v1).transitions().front().id;
    const auto range = f.track(f.v1).transitions().front().range;
    REQUIRE(f.run(edit::makeSetTransitionKind(f.project, f.on(f.v1), transitionId,
                                              model::TransitionKind::Wipe,
                                              model::TransitionDirection::Right)));

    // Half way through: the left half is the incoming shot, the right half the
    // outgoing one, and neither is a blend of the two.
    const time::RationalTime middle =
        range.start() + time::RationalTime{range.duration().frames() / 2, range.start().rate()};
    auto frame = graph.composite(f.sequence(), middle);
    REQUIRE(frame);
    CHECK(frame->at(4, 8).b == Approx(1.0F));
    CHECK(frame->at(4, 8).r == Approx(0.0F).margin(0.01F));
    CHECK(frame->at(28, 8).r == Approx(1.0F));
    CHECK(frame->at(28, 8).b == Approx(0.0F).margin(0.01F));
}

TEST_CASE("Easing moves the middle of a blend to its ends", "[render][transition]") {
    // The ends are pinned for every curve, because a transition that did not
    // start where the outgoing shot is or finish where the incoming one is
    // would be a jump either side of itself.
    for (const model::TransitionEasing easing :
         {model::TransitionEasing::Linear, model::TransitionEasing::In,
          model::TransitionEasing::Out, model::TransitionEasing::InOut}) {
        CHECK(model::easedProgress(0.0, easing) == Approx(0.0));
        CHECK(model::easedProgress(1.0, easing) == Approx(1.0));
        // And nothing runs backwards or overshoots in between.
        double previous = -1.0;
        for (int step = 0; step <= 20; ++step) {
            const double value = model::easedProgress(step / 20.0, easing);
            CHECK(value >= previous);
            CHECK(value >= Approx(0.0).margin(1e-9));
            CHECK(value <= Approx(1.0).margin(1e-9));
            previous = value;
        }
    }

    // What each curve is *for*, measured a quarter of the way in, where they
    // differ most. Slow to start is behind a constant rate there; slow to
    // finish is ahead of it; and slow at both ends is behind at a quarter and
    // ahead at three.
    CHECK(model::easedProgress(0.25, model::TransitionEasing::In) < 0.25);
    CHECK(model::easedProgress(0.25, model::TransitionEasing::Out) > 0.25);
    CHECK(model::easedProgress(0.25, model::TransitionEasing::InOut) < 0.25);
    CHECK(model::easedProgress(0.75, model::TransitionEasing::InOut) > 0.75);
    // The one point every symmetric curve has to agree on.
    CHECK(model::easedProgress(0.5, model::TransitionEasing::InOut) == Approx(0.5));

    // Out of range is clamped rather than extrapolated: a curve fed 1.4 should
    // give a finished transition, not a shot at 196% opacity.
    CHECK(model::easedProgress(1.4, model::TransitionEasing::In) == Approx(1.0));
    CHECK(model::easedProgress(-0.3, model::TransitionEasing::Out) == Approx(0.0));
}

TEST_CASE("A transition's own progress is the eased one", "[render][transition]") {
    // The reason easing lives in `progressAt` rather than in the shape: this
    // is the one question the two render paths and the audio crossfade all
    // ask, so easing here reaches the sound as well as the picture.
    const time::Rational rate{25, 1};
    model::Transition transition;
    transition.range = time::TimeRange{time::RationalTime{0, rate}, time::RationalTime{20, rate}};
    const time::RationalTime quarter{5, rate};

    CHECK(transition.progressAt(quarter) == Approx(0.25));
    transition.easing = model::TransitionEasing::In;
    CHECK(transition.progressAt(quarter) == Approx(0.0625));
    // And the ends stay put whatever the curve.
    CHECK(transition.progressAt(time::RationalTime{0, rate}) == Approx(0.0));
    CHECK(transition.progressAt(time::RationalTime{20, rate}) == Approx(1.0));
}

TEST_CASE("Softness widens a wipe's edge without softening the other three",
          "[render][transition]") {
    auto wipe = transitionOf(model::TransitionKind::Wipe, model::TransitionDirection::Right);

    // A hard wipe is exactly what it always was. This is the load-bearing
    // half: the geometry below is one formula with the padding derived from
    // the feather, so a softness of zero has to reduce to the old numbers
    // rather than to something merely close.
    const auto hard = render::transitionShapeFor(wipe, 0.25, 1000, 500);
    CHECK(hard.incoming.mask.feather == Approx(0.0));
    CHECK(hard.incoming.mask.width == Approx(250.0));
    CHECK(hard.incoming.mask.height == Approx(500.0));
    CHECK(hard.incoming.mask.centreX == Approx(-375.0));

    wipe.softness = 0.2;
    const auto soft = render::transitionShapeFor(wipe, 0.25, 1000, 500);
    // A fifth of the distance travelled, which for a horizontal wipe is the
    // width.
    CHECK(soft.incoming.mask.feather == Approx(200.0));
    // The three sides that are not travelling sit a whole ramp outside the
    // frame, so their own ramps never reach a pixel anybody sees.
    CHECK(soft.incoming.mask.height == Approx(500.0 + 400.0));
    CHECK(soft.incoming.mask.centreY == Approx(0.0));

    // And the travelling edge starts and finishes half a ramp beyond the
    // frame, so the wipe begins showing none of the incoming shot and ends
    // showing all of it -- the two moments a soft edge would otherwise get
    // wrong in opposite directions.
    const auto start = render::transitionShapeFor(wipe, 0.0, 1000, 500);
    CHECK(start.incoming.mask.centreX + (start.incoming.mask.width / 2.0) == Approx(-600.0));
    const auto end = render::transitionShapeFor(wipe, 1.0, 1000, 500);
    CHECK(end.incoming.mask.centreX + (end.incoming.mask.width / 2.0) == Approx(600.0));

    SECTION("and a vertical wipe measures its softness against the height") {
        auto down = transitionOf(model::TransitionKind::Wipe, model::TransitionDirection::Down);
        down.softness = 0.2;
        const auto shape = render::transitionShapeFor(down, 0.5, 1000, 500);
        CHECK(shape.incoming.mask.feather == Approx(100.0));
        CHECK(shape.incoming.mask.width == Approx(1000.0 + 200.0));
    }

    SECTION("and the kinds with no edge ignore it") {
        auto dissolve =
            transitionOf(model::TransitionKind::CrossDissolve, model::TransitionDirection::Right);
        dissolve.softness = 0.5;
        CHECK_FALSE(render::transitionShapeFor(dissolve, 0.5, 1000, 500).incoming.mask.isSet());

        auto slide = transitionOf(model::TransitionKind::Slide, model::TransitionDirection::Right);
        slide.softness = 0.5;
        const auto shape = render::transitionShapeFor(slide, 0.5, 1000, 500);
        CHECK_FALSE(shape.incoming.mask.isSet());
        CHECK(shape.incoming.offsetX == Approx(-500.0));
    }
}

TEST_CASE("A soft wipe blends across its edge in the picture", "[render][transition]") {
    // The discriminating measurement: a hard wipe puts one shot on each side
    // of a line and nothing in between, while a soft one has a band that is
    // neither. Made through the compositor, because the softness reaches it as
    // a mask feather and only the shader and its CPU twin know what that does.
    Fixture f;
    f.sequence().setSize(64, 16);
    SolidFrameSource source{64, 16};
    const model::MediaRefId second = f.addMedia("second.mov", 10000);
    source.define(f.longMedia, render::Rgba{1.0F, 0.0F, 0.0F, 1.0F});
    source.define(second, render::Rgba{0.0F, 0.0F, 1.0F, 1.0F});
    render::RenderGraph graph{source};

    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(50, 50, 500, second))));
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(50), f.at(20))));
    const model::TransitionId id = f.track(f.v1).transitions().front().id;
    const auto range = f.track(f.v1).transitions().front().range;
    const time::RationalTime middle =
        range.start() + time::RationalTime{range.duration().frames() / 2, range.start().rate()};

    // How many columns are a mix of the two shots rather than one or the
    // other, along the middle row.
    const auto blendedColumns = [&] {
        auto frame = graph.composite(f.sequence(), middle);
        REQUIRE(frame);
        int mixed = 0;
        for (std::int32_t x = 0; x < 64; ++x) {
            const render::Rgba pixel = frame->at(x, 8);
            if (pixel.r > 0.05F && pixel.b > 0.05F) {
                ++mixed;
            }
        }
        return mixed;
    };

    edit::TransitionSettings settings;
    settings.kind = model::TransitionKind::Wipe;
    settings.direction = model::TransitionDirection::Right;
    REQUIRE(f.run(edit::makeSetTransitionSettings(f.project, f.on(f.v1), id, settings)));
    const int hard = blendedColumns();

    settings.softness = 0.5;
    REQUIRE(f.run(edit::makeSetTransitionSettings(f.project, f.on(f.v1), id, settings)));
    const int soft = blendedColumns();

    INFO("hard wipe blended " << hard << " columns, soft " << soft);
    CHECK(hard <= 2);
    CHECK(soft > hard + 4);
}

TEST_CASE("Changing a transition's kind keeps the rest of its settings", "[render][transition]") {
    // `makeSetTransitionKind` is a call to the settings operation with the
    // other fields read back off the transition, not a second way of writing
    // one. If it ever became the latter, choosing a wipe would silently
    // discard the easing somebody had set.
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(50, 50, 500))));
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(50), f.at(20))));
    const model::TransitionId id = f.track(f.v1).transitions().front().id;

    edit::TransitionSettings eased;
    eased.easing = model::TransitionEasing::InOut;
    eased.softness = 0.4;
    REQUIRE(f.run(edit::makeSetTransitionSettings(f.project, f.on(f.v1), id, eased)));

    REQUIRE(f.run(edit::makeSetTransitionKind(
        f.project, f.on(f.v1), id, model::TransitionKind::Wipe, model::TransitionDirection::Up)));
    const model::Transition& now = f.track(f.v1).transitions().front();
    CHECK(now.kind == model::TransitionKind::Wipe);
    CHECK(now.direction == model::TransitionDirection::Up);
    CHECK(now.easing == model::TransitionEasing::InOut);
    CHECK(now.softness == Approx(0.4));

    SECTION("and a softness outside the range is clamped rather than trusted") {
        edit::TransitionSettings wild;
        wild.kind = model::TransitionKind::Wipe;
        wild.softness = 4.0;
        REQUIRE(f.run(edit::makeSetTransitionSettings(f.project, f.on(f.v1), id, wild)));
        CHECK(f.track(f.v1).transitions().front().softness == Approx(1.0));
    }
}

TEST_CASE("An iris opens from the centre and clears the corners", "[render][transition]") {
    const auto iris = transitionOf(model::TransitionKind::Iris, model::TransitionDirection::Right);

    const auto start = render::transitionShapeFor(iris, 0.0, 1000, 500);
    REQUIRE(start.incoming.mask.shape == model::MaskShape::Ellipse);
    CHECK(start.incoming.mask.width == Approx(0.0));
    CHECK(start.incoming.mask.height == Approx(0.0));
    // It opens from the middle, so it has no offset and no opacity of its own.
    CHECK(start.incoming.mask.centreX == Approx(0.0));
    CHECK(start.incoming.opacity == Approx(1.0));

    // At the end it circumscribes the frame rather than merely filling it: an
    // ellipse that reached only the frame's width would leave four corners
    // unrevealed, which is the whole failure mode this factor exists for.
    const auto end = render::transitionShapeFor(iris, 1.0, 1000, 500);
    CHECK(end.incoming.mask.width > 1000.0);
    CHECK(end.incoming.mask.height > 500.0);
    // The corner of the frame is inside it: (x/a)^2 + (y/b)^2 <= 1.
    const double a = end.incoming.mask.width / 2.0;
    const double b = end.incoming.mask.height / 2.0;
    CHECK((((500.0 / a) * (500.0 / a)) + ((250.0 / b) * (250.0 / b))) <= 1.0 + 1e-9);

    SECTION("and a direction means nothing to it") {
        for (const model::TransitionDirection direction :
             {model::TransitionDirection::Left, model::TransitionDirection::Up}) {
            const auto other = render::transitionShapeFor(
                transitionOf(model::TransitionKind::Iris, direction), 0.5, 1000, 500);
            CHECK(other.incoming.mask.width ==
                  Approx(render::transitionShapeFor(iris, 0.5, 1000, 500).incoming.mask.width));
        }
    }

    SECTION("and softening it keeps both ends absolute") {
        auto soft = iris;
        soft.softness = 0.3;
        // A fraction of the smaller side, which is the distance that decides
        // how soft the curve looks against the frame.
        CHECK(render::transitionShapeFor(soft, 0.5, 1000, 500).incoming.mask.feather ==
              Approx(150.0));
        // Nothing at all at the start: a negative half-extent is no ellipse,
        // which is what keeps a soft iris from opening already ajar.
        CHECK(render::transitionShapeFor(soft, 0.0, 1000, 500).incoming.mask.width < 0.0);
        // And past the corners at the end, by half a ramp.
        const auto full = render::transitionShapeFor(soft, 1.0, 1000, 500);
        CHECK(full.incoming.mask.width == Approx((1000.0 * 1.41421356237309505) + 150.0));
    }
}

TEST_CASE("A zoom grows the incoming shot and moves nothing", "[render][transition]") {
    const auto zoom = transitionOf(model::TransitionKind::Zoom, model::TransitionDirection::Right);

    const auto start = render::transitionShapeFor(zoom, 0.0, 1000, 500);
    CHECK(start.incoming.scaleX == Approx(0.0));
    CHECK(start.incoming.scaleY == Approx(0.0));
    // It is a transform and nothing else: no mask, no offset, and full
    // opacity throughout. A zoom that also faded would be two transitions.
    CHECK_FALSE(start.incoming.mask.isSet());
    CHECK(start.incoming.offsetX == Approx(0.0));
    CHECK(start.incoming.opacity == Approx(1.0));

    CHECK(render::transitionShapeFor(zoom, 0.5, 1000, 500).incoming.scaleX == Approx(0.5));
    const auto end = render::transitionShapeFor(zoom, 1.0, 1000, 500);
    CHECK(end.incoming.scaleX == Approx(1.0));
    CHECK(end.incoming.scaleY == Approx(1.0));

    SECTION("and every other kind leaves the scale alone") {
        for (const model::TransitionKind kind :
             {model::TransitionKind::CrossDissolve, model::TransitionKind::Wipe,
              model::TransitionKind::Slide, model::TransitionKind::Iris}) {
            const auto shape = render::transitionShapeFor(
                transitionOf(kind, model::TransitionDirection::Right), 0.5, 1000, 500);
            INFO("kind " << model::toString(kind));
            CHECK(shape.incoming.scaleX == Approx(1.0));
            CHECK(shape.incoming.scaleY == Approx(1.0));
        }
    }
}

TEST_CASE("An iris and a zoom reach the picture", "[render][transition]") {
    // The discriminating measurement, and the reason it is worth making at
    // all: both kinds are expressed through machinery the compositor already
    // had -- an elliptical mask and a scale -- so the thing that can go wrong
    // is not the maths but the wiring. A shape field nothing reads is a
    // transition that renders as a cut.
    Fixture f;
    f.sequence().setSize(64, 64);
    SolidFrameSource source{64, 64};
    const model::MediaRefId second = f.addMedia("second.mov", 10000);
    source.define(f.longMedia, render::Rgba{1.0F, 0.0F, 0.0F, 1.0F});
    source.define(second, render::Rgba{0.0F, 0.0F, 1.0F, 1.0F});
    render::RenderGraph graph{source};

    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(50, 50, 500, second))));
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(50), f.at(20))));
    const model::TransitionId id = f.track(f.v1).transitions().front().id;
    const auto range = f.track(f.v1).transitions().front().range;
    const time::RationalTime middle =
        range.start() + time::RationalTime{range.duration().frames() / 2, range.start().rate()};

    const auto asKind = [&](model::TransitionKind kind) {
        edit::TransitionSettings settings;
        settings.kind = kind;
        REQUIRE(f.run(edit::makeSetTransitionSettings(f.project, f.on(f.v1), id, settings)));
        auto frame = graph.composite(f.sequence(), middle);
        REQUIRE(frame);
        return std::pair{frame->at(32, 32), frame->at(2, 2)};
    };

    // Both put the incoming shot in the middle of the frame and leave the
    // outgoing one in the corner, which is what "opens from the centre" and
    // "grows from nothing in the centre" have in common -- and is exactly what
    // a dissolve does not do.
    for (const model::TransitionKind kind :
         {model::TransitionKind::Iris, model::TransitionKind::Zoom}) {
        const auto [centre, corner] = asKind(kind);
        INFO("kind " << model::toString(kind) << " centre b=" << centre.b
                     << " corner r=" << corner.r);
        CHECK(centre.b > 0.5F);
        CHECK(corner.r > 0.5F);
        CHECK(corner.b < 0.5F);
    }

    // The control: a dissolve blends both places the same way, so a kind that
    // silently fell back to one would read the same in the centre and the
    // corner and this test would not tell them apart.
    const auto [dissolveCentre, dissolveCorner] = asKind(model::TransitionKind::CrossDissolve);
    CHECK(dissolveCentre.b == Approx(dissolveCorner.b).margin(0.01F));
}

TEST_CASE("A push takes the outgoing shot with it", "[render][transition]") {
    const auto push = transitionOf(model::TransitionKind::Push, model::TransitionDirection::Right);
    const auto slide =
        transitionOf(model::TransitionKind::Slide, model::TransitionDirection::Right);

    // The incoming half is a slide, exactly: the two share the arithmetic, and
    // the whole difference between them is what happens to the other clip.
    for (const double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const auto pushed = render::transitionShapeFor(push, t, 1000, 500);
        const auto slid = render::transitionShapeFor(slide, t, 1000, 500);
        INFO("at " << t);
        CHECK(pushed.incoming.offsetX == Approx(slid.incoming.offsetX));
        // And a slide leaves the outgoing clip alone, which is the thing a
        // push does not do.
        CHECK(slid.outgoing.offsetX == Approx(0.0));
    }

    // The outgoing clip starts at home and ends a whole frame away, travelling
    // the same direction as the incoming one -- ahead of it, not against it.
    CHECK(render::transitionShapeFor(push, 0.0, 1000, 500).outgoing.offsetX == Approx(0.0));
    CHECK(render::transitionShapeFor(push, 0.5, 1000, 500).outgoing.offsetX == Approx(500.0));
    CHECK(render::transitionShapeFor(push, 1.0, 1000, 500).outgoing.offsetX == Approx(1000.0));

    // The two are always exactly a frame apart, which is what makes a push
    // read as one continuous move rather than as two clips sliding at once.
    for (const double t : {0.0, 0.3, 0.7, 1.0}) {
        const auto shape = render::transitionShapeFor(push, t, 1000, 500);
        INFO("at " << t);
        CHECK(shape.outgoing.offsetX - shape.incoming.offsetX == Approx(1000.0));
    }

    SECTION("and the other three directions mirror it") {
        const auto left = render::transitionShapeFor(
            transitionOf(model::TransitionKind::Push, model::TransitionDirection::Left), 0.5, 1000,
            500);
        CHECK(left.outgoing.offsetX == Approx(-500.0));
        CHECK(left.incoming.offsetX == Approx(500.0));

        const auto up = render::transitionShapeFor(
            transitionOf(model::TransitionKind::Push, model::TransitionDirection::Up), 0.5, 1000,
            500);
        CHECK(up.outgoing.offsetY == Approx(-250.0));
        CHECK(up.incoming.offsetY == Approx(250.0));
        CHECK(up.outgoing.offsetX == Approx(0.0));
    }
}

TEST_CASE("A dip goes down to nothing and back up", "[render][transition]") {
    const auto dip =
        transitionOf(model::TransitionKind::DipToBlack, model::TransitionDirection::Right);

    // Two fades end to end. The outgoing shot has the first half to itself and
    // the incoming one the second, and they never overlap -- which is what
    // makes this a dip rather than a slow dissolve.
    const auto start = render::transitionShapeFor(dip, 0.0, 1000, 500);
    CHECK(start.outgoing.opacity == Approx(1.0));
    CHECK(start.incoming.opacity == Approx(0.0));

    const auto quarter = render::transitionShapeFor(dip, 0.25, 1000, 500);
    CHECK(quarter.outgoing.opacity == Approx(0.5));
    CHECK(quarter.incoming.opacity == Approx(0.0));

    // The bottom: neither clip is drawn at all, and the frame is black because
    // nothing is on it rather than because anything painted it black.
    const auto bottom = render::transitionShapeFor(dip, 0.5, 1000, 500);
    CHECK(bottom.outgoing.opacity == Approx(0.0));
    CHECK(bottom.incoming.opacity == Approx(0.0));

    const auto threeQuarters = render::transitionShapeFor(dip, 0.75, 1000, 500);
    CHECK(threeQuarters.outgoing.opacity == Approx(0.0));
    CHECK(threeQuarters.incoming.opacity == Approx(0.5));

    const auto end = render::transitionShapeFor(dip, 1.0, 1000, 500);
    CHECK(end.outgoing.opacity == Approx(0.0));
    CHECK(end.incoming.opacity == Approx(1.0));

    // Nothing moves and nothing is masked: a dip is opacity and nothing else,
    // on both sides.
    CHECK(bottom.incoming.offsetX == Approx(0.0));
    CHECK(bottom.outgoing.offsetX == Approx(0.0));
    CHECK_FALSE(bottom.incoming.mask.isSet());
    CHECK_FALSE(bottom.outgoing.mask.isSet());
}

TEST_CASE("Only a push and a dip touch the outgoing side", "[render][transition]") {
    // The invariant the split has to keep: adding an outgoing side must not
    // have quietly changed what every existing kind does to the clip it leaves
    // behind. A default side composes to the identity, so a kind that sets
    // nothing there draws the outgoing clip exactly as it did before.
    for (const model::TransitionKind kind :
         {model::TransitionKind::CrossDissolve, model::TransitionKind::Wipe,
          model::TransitionKind::Slide, model::TransitionKind::Iris, model::TransitionKind::Zoom}) {
        for (const double t : {0.0, 0.5, 1.0}) {
            const auto shape = render::transitionShapeFor(
                transitionOf(kind, model::TransitionDirection::Right), t, 1000, 500);
            INFO("kind " << model::toString(kind) << " at " << t);
            CHECK(shape.outgoing.opacity == Approx(1.0));
            CHECK(shape.outgoing.offsetX == Approx(0.0));
            CHECK(shape.outgoing.offsetY == Approx(0.0));
            CHECK(shape.outgoing.scaleX == Approx(1.0));
            CHECK(shape.outgoing.scaleY == Approx(1.0));
            CHECK_FALSE(shape.outgoing.mask.isSet());
        }
    }
}

TEST_CASE("Composing a side leaves the clip's own transform in it", "[render][transition]") {
    // Multiplied and added rather than assigned. A clip somebody has already
    // faded, moved and scaled should arrive that way: the transition says what
    // it does *to* the clip, not what the clip is.
    model::Transform own;
    own.opacity = 0.5;
    own.positionX = 100.0;
    own.scaleX = 2.0;

    render::TransitionSide side;
    side.opacity = 0.5;
    side.offsetX = -40.0;
    side.scaleX = 0.25;

    const model::Transform composed = render::shapedTransform(own, side);
    CHECK(composed.opacity == Approx(0.25));
    CHECK(composed.positionX == Approx(60.0));
    CHECK(composed.scaleX == Approx(0.5));
    // Untouched fields come through as they were.
    CHECK(composed.scaleY == Approx(own.scaleY));
    CHECK(composed.positionY == Approx(own.positionY));

    // And a default side is the identity, which is what lets every kind that
    // leaves a clip alone simply not mention it.
    const model::Transform untouched = render::shapedTransform(own, render::TransitionSide{});
    CHECK(untouched.opacity == Approx(own.opacity));
    CHECK(untouched.positionX == Approx(own.positionX));
    CHECK(untouched.scaleX == Approx(own.scaleX));
}

TEST_CASE("A push and a dip reach the picture", "[render][transition]") {
    Fixture f;
    f.sequence().setSize(64, 16);
    SolidFrameSource source{64, 16};
    const model::MediaRefId second = f.addMedia("second.mov", 10000);
    source.define(f.longMedia, render::Rgba{1.0F, 0.0F, 0.0F, 1.0F});
    source.define(second, render::Rgba{0.0F, 0.0F, 1.0F, 1.0F});
    render::RenderGraph graph{source};

    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(50, 50, 500, second))));
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(50), f.at(20))));
    const model::TransitionId id = f.track(f.v1).transitions().front().id;
    const auto range = f.track(f.v1).transitions().front().range;
    const time::RationalTime middle =
        range.start() + time::RationalTime{range.duration().frames() / 2, range.start().rate()};

    // The frame is move-only, so this sets the kind and the samples are taken
    // where they are read rather than handed back.
    const auto asKind = [&](model::TransitionKind kind) {
        edit::TransitionSettings settings;
        settings.kind = kind;
        REQUIRE(f.run(edit::makeSetTransitionSettings(f.project, f.on(f.v1), id, settings)));
    };

    // A dip at its midpoint shows neither shot: the outgoing one has gone and
    // the incoming one has not arrived. This is the measurement a one-sided
    // shape could not have made, because it had no way to turn the outgoing
    // clip off.
    asKind(model::TransitionKind::DipToBlack);
    {
        auto frame = graph.composite(f.sequence(), middle);
        REQUIRE(frame);
        for (const std::int32_t x : {2, 32, 61}) {
            INFO("dip at x=" << x);
            CHECK(frame->at(x, 8).r == Approx(0.0F).margin(0.02F));
            CHECK(frame->at(x, 8).b == Approx(0.0F).margin(0.02F));
        }
    }

    // A push at its midpoint has the outgoing shot shoved half off to the
    // right and the incoming one half on from the left, so the left of the
    // frame is the new shot and the right the old one -- and neither is a
    // blend, which is what tells a push from a dissolve.
    asKind(model::TransitionKind::Push);
    {
        auto frame = graph.composite(f.sequence(), middle);
        REQUIRE(frame);
        CHECK(frame->at(8, 8).b > 0.5F);
        CHECK(frame->at(8, 8).r < 0.1F);
        CHECK(frame->at(56, 8).r > 0.5F);
        CHECK(frame->at(56, 8).b < 0.1F);
    }

    // The control: a slide leaves the outgoing shot exactly where it is, so
    // the old shot still covers the whole frame under the new one arriving. A
    // push that quietly failed to move it would read the same as this.
    asKind(model::TransitionKind::Slide);
    {
        auto frame = graph.composite(f.sequence(), middle);
        REQUIRE(frame);
        CHECK(frame->at(56, 8).r > 0.5F);
    }
}

TEST_CASE("A transition kind survives a round trip through its name", "[render][transition]") {
    for (const model::TransitionKind kind :
         {model::TransitionKind::CrossDissolve, model::TransitionKind::Wipe,
          model::TransitionKind::Slide, model::TransitionKind::Iris, model::TransitionKind::Zoom,
          model::TransitionKind::Push, model::TransitionKind::DipToBlack}) {
        CHECK(model::transitionKindFromString(model::toString(kind)) == kind);
    }

    // What each kind offers, asked of the model so that the shape and the
    // panel cannot come to different answers. A slide travels and has no edge;
    // an iris has an edge and does not travel; a zoom has neither.
    CHECK(model::transitionTravels(model::TransitionKind::Wipe));
    CHECK(model::transitionTravels(model::TransitionKind::Slide));
    CHECK(model::transitionTravels(model::TransitionKind::Push));
    CHECK_FALSE(model::transitionTravels(model::TransitionKind::CrossDissolve));
    CHECK_FALSE(model::transitionTravels(model::TransitionKind::Iris));
    CHECK_FALSE(model::transitionTravels(model::TransitionKind::Zoom));
    CHECK_FALSE(model::transitionTravels(model::TransitionKind::DipToBlack));

    CHECK(model::transitionHasEdge(model::TransitionKind::Wipe));
    CHECK(model::transitionHasEdge(model::TransitionKind::Iris));
    CHECK_FALSE(model::transitionHasEdge(model::TransitionKind::Slide));
    CHECK_FALSE(model::transitionHasEdge(model::TransitionKind::Zoom));
    CHECK_FALSE(model::transitionHasEdge(model::TransitionKind::Push));
    CHECK_FALSE(model::transitionHasEdge(model::TransitionKind::CrossDissolve));
    CHECK_FALSE(model::transitionHasEdge(model::TransitionKind::DipToBlack));
    // A name from a later version is a cut somebody can still watch, so it
    // falls back rather than refusing the project.
    CHECK(model::transitionKindFromString("morphCut") == model::TransitionKind::CrossDissolve);

    for (const model::TransitionEasing easing :
         {model::TransitionEasing::Linear, model::TransitionEasing::In,
          model::TransitionEasing::Out, model::TransitionEasing::InOut}) {
        CHECK(model::transitionEasingFromString(model::toString(easing)) == easing);
    }
    // And a curve this build has never heard of is a constant rate, by the
    // same rule for the same reason.
    CHECK(model::transitionEasingFromString("bounce") == model::TransitionEasing::Linear);

    for (const model::TransitionDirection direction :
         {model::TransitionDirection::Right, model::TransitionDirection::Left,
          model::TransitionDirection::Down, model::TransitionDirection::Up}) {
        model::TransitionDirection back{};
        REQUIRE(model::transitionDirectionFromString(model::toString(direction), back));
        CHECK(back == direction);
    }
}

TEST_CASE("A fade obeys its kind instead of always dissolving", "[render][transition]") {
    // The bug, reported from the running app: the Type control appeared to do
    // nothing. It worked across a cut and did nothing at the end of a run,
    // because the one-sided branch ramped an opacity of its own and never
    // asked what kind of transition this was. Every kind looked like a
    // dissolve there, which made the control a lie wherever a span had one
    // side empty -- and a span at the head or tail of a track always does.
    Fixture f;
    f.sequence().setSize(64, 16);
    SolidFrameSource source{64, 16};
    source.define(f.longMedia, render::Rgba{1.0F, 1.0F, 1.0F, 1.0F});
    render::RenderGraph graph{source};

    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50, 500))));
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(50), f.at(20))));
    const model::Transition& fade = f.track(f.v1).transitions().front();
    REQUIRE(fade.isFadeOut());
    const model::TransitionId id = fade.id;
    const auto range = fade.range;
    const time::RationalTime middle =
        range.start() + time::RationalTime{range.duration().frames() / 2, range.start().rate()};

    const auto asKind = [&](model::TransitionKind kind) {
        edit::TransitionSettings settings;
        settings.kind = kind;
        settings.direction = model::TransitionDirection::Right;
        REQUIRE(f.run(edit::makeSetTransitionSettings(f.project, f.on(f.v1), id, settings)));
    };

    // A dissolve is the behaviour this branch always had: an even ramp, so
    // both ends of the frame read the same part of the way through.
    asKind(model::TransitionKind::CrossDissolve);
    {
        auto frame = graph.composite(f.sequence(), middle);
        REQUIRE(frame);
        const float left = frame->at(2, 8).a;
        const float right = frame->at(61, 8).a;
        INFO("dissolve fade: left " << left << " right " << right);
        CHECK(left == Approx(right).margin(0.02F));
        // Part way gone, rather than fully there or fully absent.
        CHECK(left > 0.2F);
        CHECK(left < 0.8F);
    }

    // A wipe uncovers the shot against black rather than blending it, so the
    // two ends of the frame are nothing alike. This is what read as a dissolve
    // before, and what the panel promised all along.
    asKind(model::TransitionKind::Wipe);
    {
        auto frame = graph.composite(f.sequence(), middle);
        REQUIRE(frame);
        const float left = frame->at(2, 8).a;
        const float right = frame->at(61, 8).a;
        INFO("wipe fade: left " << left << " right " << right);
        CHECK(std::fabs(left - right) > 0.5F);
    }

    // And a slide takes the shot off the frame rather than fading it, so what
    // is left where the picture was is nothing at all.
    asKind(model::TransitionKind::Slide);
    {
        auto frame = graph.composite(f.sequence(), middle);
        REQUIRE(frame);
        // Half a frame off to the left at the midpoint: the right-hand end
        // still holds picture, the left-hand end has been vacated.
        INFO("slide fade: left " << frame->at(2, 8).a << " right " << frame->at(61, 8).a);
        CHECK(std::fabs(frame->at(2, 8).a - frame->at(61, 8).a) > 0.5F);
    }
}

TEST_CASE("A fade in runs its kind the other way round", "[render][transition]") {
    // A fade out is a fade in played backwards, which is the whole reason one
    // shape serves both. If the direction of travel were not reversed, a fade
    // in would end with the shot half uncovered.
    Fixture f;
    f.sequence().setSize(64, 16);
    SolidFrameSource source{64, 16};
    source.define(f.longMedia, render::Rgba{1.0F, 1.0F, 1.0F, 1.0F});
    render::RenderGraph graph{source};

    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50, 500))));
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(0), f.at(20))));
    const model::Transition& fade = f.track(f.v1).transitions().front();
    REQUIRE(fade.isFadeIn());

    edit::TransitionSettings settings;
    settings.kind = model::TransitionKind::Wipe;
    settings.direction = model::TransitionDirection::Right;
    REQUIRE(f.run(edit::makeSetTransitionSettings(f.project, f.on(f.v1), fade.id, settings)));

    const auto range = f.track(f.v1).transitions().front().range;
    // At the end of the span the shot is fully there, whatever the kind: a
    // transition that did not finish where its clip is would be a jump.
    auto arrived = graph.composite(f.sequence(), range.endExclusive());
    REQUIRE(arrived);
    CHECK(arrived->at(2, 8).a == Approx(1.0F).margin(0.02F));
    CHECK(arrived->at(61, 8).a == Approx(1.0F).margin(0.02F));

    // And part way through it is uncovering rather than blending.
    const time::RationalTime middle =
        range.start() + time::RationalTime{range.duration().frames() / 2, range.start().rate()};
    auto part = graph.composite(f.sequence(), middle);
    REQUIRE(part);
    INFO("wipe fade in: left " << part->at(2, 8).a << " right " << part->at(61, 8).a);
    CHECK(std::fabs(part->at(2, 8).a - part->at(61, 8).a) > 0.5F);
}
