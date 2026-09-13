// Which way up a frame comes back off the GPU, on every backend this program
// can be handed.
//
// **Why this is in the GUI suite and not beside the other compositor tests.**
// Those build their own device with `GpuCompositor::create()`, which picks one
// backend per platform: Metal on macOS, D3D11 on Windows, Vulkan on Linux. The
// preview does not use that device at all -- it adopts the one QRhiWidget
// already has, and on Linux that is **OpenGL**, which none of those tests ever
// touch. So the one backend whose framebuffer origin differs from all the
// others was the one nothing covered.
//
// OpenGL puts (0, 0) at the bottom left of a render target; every other backend
// puts it at the top left. A frame read back off the GPU therefore arrives
// bottom-up there and top-down everywhere else, and a check that counts lit
// pixels cannot tell the two apart -- which is how upside-down video reached
// Linux while every test was green.
//
// Making a GL device needs a QOffscreenSurface, which needs a QGuiApplication,
// which this suite has and the media tests deliberately do not.

#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <cstdint>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include <rhi/qrhi.h>

#include "zaro/core/Error.h"
#include "zaro/core/model/Clip.h"
#include "zaro/core/render/Compositing.h"
#include "zaro/platform/qrhi/GpuCompositor.h"

#include "GuiFixture.h"

using namespace zaro;
using render::Rgba;
using render::RgbaImage;

namespace {

/// White across the top third, black below: a picture that says which way up
/// it is. A flat one does not, which is the whole reason the flip survived.
RgbaImage topHeavy(std::int32_t width, std::int32_t height) {
    RgbaImage image{width, height};
    for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
            const float v = y < height / 3 ? 1.0F : 0.0F;
            image.at(x, y) = Rgba{v, v, v, 1.0F};
        }
    }
    return image;
}

/// A device on a named backend, or nothing when this machine has no such thing.
struct Device {
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QRhi> rhi;
};

Device openGlDevice() {
    Device device;
    // Qt's own helper: it makes a surface with the format a GL QRhi needs, and
    // it is the only supported way to get one.
    device.surface.reset(QRhiGles2InitParams::newFallbackSurface());
    if (!device.surface) {
        return {};
    }
    QRhiGles2InitParams params;
    params.fallbackSurface = device.surface.get();
    device.rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
    if (!device.rhi) {
        device.surface.reset();
    }
    return device;
}

}  // namespace

TEST_CASE("A frame read back off the GPU is the right way up", "[gui][gpu]") {
    // The application before the device: a GL device needs a surface, a surface
    // is built by the platform integration, and the platform integration
    // exists only once a QGuiApplication does. Without this the test crashes
    // inside Qt when it is the only one asked for.
    static_cast<void>(zaro::app::testing::gui());

    Device device = openGlDevice();
    if (!device.rhi) {
        SKIP("no OpenGL device on this machine");
    }
    INFO("backend: " << device.rhi->backendName()
                     << ", y-up framebuffer: " << device.rhi->isYUpInFramebuffer());

    auto adopted = platform::qrhi::GpuCompositor::adopt(*device.rhi);
    if (!adopted) {
        SKIP("the compositor cannot use this device");
    }
    platform::qrhi::GpuCompositor& compositor = **adopted;

    const auto ok = [](Status status) {
        if (!status) {
            FAIL(status.error().toString());
        }
    };

    const RgbaImage source = topHeavy(32, 30);
    ok(compositor.beginFrame(32, 30));
    ok(compositor.draw(source, model::Transform{}, model::BlendMode::Normal));

    SECTION("composited and read straight back") {
        RgbaImage out;
        ok(compositor.endFrame(out));
        CHECK(out.at(16, 2).r > 0.5F);   // near the top: white, as it went in
        CHECK(out.at(16, 27).r < 0.5F);  // near the bottom: black
    }

    SECTION("presented into a target and read back") {
        ok(compositor.endFrameOnGpu());
        RgbaImage presented;
        // Same aspect, so there are no letterbox bars to reason around.
        ok(compositor.presentToImage(64, 60, presented));
        CHECK(presented.at(32, 5).r > 0.5F);
        CHECK(presented.at(32, 50).r < 0.5F);
    }
}
