#include <cmath>
#include <cstdint>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "zaro/core/render/HalfImage.h"

using namespace zaro;
using Catch::Approx;

TEST_CASE("Half precision round-trips the values it can hold exactly", "[render][half]") {
    for (float value :
         {0.0F, 1.0F, -1.0F, 0.5F, 2.0F, 1024.0F, 65504.0F, 6.103515625e-05F, 5.9604645e-08F}) {
        INFO(value);
        CHECK(render::floatFromHalf(render::halfFromFloat(value)) == value);
    }
    CHECK(render::halfFromFloat(1.0F) == 0x3c00);
    CHECK(render::halfFromFloat(-2.0F) == 0xc000);
    CHECK(render::halfFromFloat(65504.0F) == 0x7bff);
}

TEST_CASE("Half precision rounds to nearest, even on a tie", "[render][half]") {
    // Between 1 and 1 + 2^-10 the tie goes to the even one, 1.
    CHECK(render::halfFromFloat(1.0F + std::ldexp(1.0F, -11)) == 0x3c00);
    // Between 1 + 2^-10 and 1 + 2^-9 it goes up, to the even one.
    CHECK(render::halfFromFloat(1.0F + (3.0F * std::ldexp(1.0F, -11))) == 0x3c02);
    // Scene-linear values across the range stay within half an ulp.
    for (float value = 1e-4F; value < 60000.0F; value *= 1.37F) {
        const float back = render::floatFromHalf(render::halfFromFloat(value));
        INFO(value);
        CHECK(std::fabs(back - value) <= value * std::ldexp(1.0F, -11));
    }
}

TEST_CASE("Half precision keeps the edges of the range", "[render][half]") {
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(render::halfFromFloat(inf) == 0x7c00);
    CHECK(render::halfFromFloat(-inf) == 0xfc00);
    CHECK(render::halfFromFloat(1e6F) == 0x7c00);
    CHECK(std::isnan(render::floatFromHalf(render::halfFromFloat(std::nanf("")))));
    CHECK(std::isinf(render::floatFromHalf(0x7c00)));
    // Below half the smallest subnormal is zero, and keeps its sign.
    CHECK(render::halfFromFloat(1e-9F) == 0x0000);
    CHECK(render::halfFromFloat(-1e-9F) == 0x8000);
    // The smallest subnormal and the largest.
    CHECK(render::floatFromHalf(0x0001) == std::ldexp(1.0F, -24));
    CHECK(render::floatFromHalf(0x03ff) == Approx(6.097555e-05F));
}

TEST_CASE("A half image packs and expands a frame", "[render][half]") {
    render::RgbaImage source{3, 2};
    source.fill(render::Rgba{0.25F, 1.5F, 12.0F, 1.0F});
    source.at(2, 1) = render::Rgba{0.1F, 0.2F, 0.3F, 0.5F};

    const render::HalfImage packed{source};
    CHECK(packed.width() == 3);
    CHECK(packed.height() == 2);
    CHECK(packed.byteSize() == source.byteSize() / 2);

    render::RgbaImage back;
    packed.expandInto(back);
    REQUIRE(back.width() == 3);
    REQUIRE(back.height() == 2);
    CHECK(back.at(0, 0).g == 1.5F);
    CHECK(back.at(0, 0).b == 12.0F);
    CHECK(back.at(2, 1).r == Approx(0.1F).margin(1e-4));
    CHECK(back.at(2, 1).a == 0.5F);
}
