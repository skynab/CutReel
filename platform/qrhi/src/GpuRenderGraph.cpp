#include "zaro/platform/qrhi/GpuRenderGraph.h"

#include "zaro/core/render/Grade.h"
#include "zaro/core/render/ShapeRaster.h"
#include "zaro/core/render/TextRasterizer.h"
#include "zaro/core/render/VideoWalk.h"

namespace zaro::platform::qrhi {

/// Draw one clip, with everything that applies to it.
///
/// A single place, for the same reason the CPU graph has one: the three call
/// sites below each used to compute the grade for themselves and they drifted
/// apart, leaving one half of a transition without its tone curves.
bool GpuRenderGraph::drawClipImage(const model::Clip& clip, const render::RgbaImage& image,
                                   const model::Transform& transform, const time::RationalTime& at,
                                   const model::Mask* wipe) {
    const render::SecondaryConstants secondary =
        render::secondaryConstantsFor(clip.secondary, transfer_);
    const render::LutTable* lut =
        clip.lut.isSet() ? luts_.tableFor(clip.lut.path, transfer_) : nullptr;
    const render::KeyerConstants keyer = render::keyerConstantsFor(clip.keyer, transfer_);
    // Sampled, not read off the clip: a tracked mask moves frame by frame, and
    // the preview showing it where it was drawn while the export shows it where
    // it was tracked to is the worst of both.
    const model::Mask mask = clip.maskAt(at);
    return compositor_
        ->draw(
            image, transform, clip.blend, render::gradeConstantsFor(clip.colorAt(at), clip.wheels),
            &curves_.tableFor(clip.id.value(), clip.curves, transfer_), &secondary, lut,
            static_cast<float>(clip.lut.amount), mask.isSet() ? &mask : nullptr,
            keyer.isActive() ? &keyer : nullptr, clip.vignette.isSet() ? &clip.vignette : nullptr,
            wipe, &colorCurves_.tableFor(clip.id.value(), clip.colorCurves))
        .ok();
}

bool GpuRenderGraph::drawClip(const model::Clip& clip, const media::VideoFrame& frame,
                              const model::Transform& transform, const time::RationalTime& at,
                              const model::Mask* wipe) {
    const render::SecondaryConstants secondary =
        render::secondaryConstantsFor(clip.secondary, transfer_);
    const render::LutTable* lut =
        clip.lut.isSet() ? luts_.tableFor(clip.lut.path, transfer_) : nullptr;
    const render::KeyerConstants keyer = render::keyerConstantsFor(clip.keyer, transfer_);
    const model::Mask mask = clip.maskAt(at);
    return compositor_
        ->drawSource(frame, transform, render::gradeConstantsFor(clip.colorAt(at), clip.wheels),
                     clip.blend, &curves_.tableFor(clip.id.value(), clip.curves, transfer_),
                     &secondary, lut, static_cast<float>(clip.lut.amount),
                     mask.isSet() ? &mask : nullptr, keyer.isActive() ? &keyer : nullptr,
                     clip.vignette.isSet() ? &clip.vignette : nullptr, wipe,
                     &colorCurves_.tableFor(clip.id.value(), clip.colorCurves))
        .ok();
}

render::RenderGraph* GpuRenderGraph::cpuGraph() {
    if (nestedSource_ == nullptr) {
        return nullptr;
    }
    if (nested_ == nullptr) {
        nested_ = std::make_unique<render::RenderGraph>(*nestedSource_);
        nested_->setProject(project_);
        nested_->setTextRasterizer(text_);
        nested_->setRenderCache(cache_);
    }
    return nested_.get();
}

bool GpuRenderGraph::drawTransitionSide(const model::Clip& clip, const model::Sequence& sequence,
                                        const model::Transform& transform,
                                        const time::RationalTime& at, render::RgbaImage& scratch,
                                        const model::Mask* wipe) {
    if (clip.graphic.isSet()) {
        if (scratch.width() != sequence.width() || scratch.height() != sequence.height()) {
            scratch = render::RgbaImage{sequence.width(), sequence.height()};
        }
        if (clip.graphic.kind == model::GraphicKind::Text) {
            if (!render::drawText(clip.graphic, text_, scratch,
                                  clip.parameterAt(model::Param::TextReveal, at))) {
                // No font engine, or it failed. A missing title is a visible,
                // diagnosable gap; a failed render is a stalled edit.
                return false;
            }
        } else {
            render::drawShape(clip.graphic, scratch);
        }
        return drawClipImage(clip, scratch, transform, at, wipe);
    }
    if (clip.nested.isValid()) {
        // A nest is composited on the CPU and uploaded: this compositor has no
        // way to render a whole sequence into a texture mid-pass.
        //
        // It was the last kind of clip a transition could not draw -- a dissolve
        // onto a nested sequence drew nothing, silently, because a clip whose
        // picture cannot be resolved is treated as a gap rather than an error.
        // That cannot recur by this route now: every clip on the frame comes
        // through here, so a kind this function cannot answer for is a kind
        // nothing can draw, which is a visible gap rather than a case that
        // works on one path and not the other.
        //
        // `composite` hands back a frame by value, so the two halves of a
        // transition do not need separate buffers here the way they do on the
        // CPU side -- each call owns what it returns.
        if (project_ == nullptr) {
            return false;
        }
        const model::Sequence* inner = project_->findSequence(clip.nested);
        if (inner == nullptr) {
            return false;
        }
        render::RenderGraph* innerGraph = cpuGraph();
        if (innerGraph == nullptr) {
            return false;
        }
        auto composed = innerGraph->composite(*inner, clip.sourceTimeAt(at));
        if (!composed) {
            return false;
        }
        return drawClipImage(clip, *composed, transform, at, wipe);
    }
    auto frame = provider_->sourceFrameFor(clip.activeSource(), clip.activeSourceTimeAt(at));
    if (!frame) {
        return false;
    }
    return drawClip(clip, **frame, transform, at, wipe);
}

bool GpuRenderGraph::needsCpuFallback(const model::Sequence& sequence, const time::RationalTime& at,
                                      const model::Project* project) {
    for (const model::Track& track : sequence.videoTracks()) {
        if (!sequence.isAudible(track)) {
            continue;
        }
        const model::Clip* clip = track.clipAt(at);
        if (clip == nullptr || !clip->enabled) {
            continue;
        }
        // An adjustment layer needs the accumulated frame read back; an effect
        // needs a pixel's neighbours, which this compositor's single sampling
        // pass cannot reach. Both are things the CPU graph already does
        // correctly, and neither is on the path that delivers.
        // A path mask is a third: its coverage is a scanline fill of the whole
        // frame, not a formula a fragment shader can answer per pixel from a
        // handful of uniforms. Handing the GPU a coverage texture per clip per
        // frame is the fast path and is worth having; the CPU graph already
        // produces exactly the right answer, and Phase 5w's render cache is
        // what keeps that affordable in the meantime.
        if (clip->adjustment || model::anyActive(clip->effects) ||
            clip->mask.shape == model::MaskShape::Path) {
            return true;
        }
        // A still is a fourth. A .png or a .tiff decodes to packed RGB, and
        // this compositor uploads one R8 texture per plane and undoes a Y'CbCr
        // matrix in the shader -- it has no packed-RGB path, and handed one it
        // would read the interleaved bytes as a luma plane and show something
        // that is not the picture.
        //
        // Sent to the CPU graph rather than given a shader of its own, for the
        // same reason as the three above: that graph already produces exactly
        // the right answer, and it is what the export uses, so the preview and
        // the delivered file cannot disagree. The cost is the one thing a still
        // does not mind paying -- it is decoded once and held in the frame
        // cache, so the work per frame is a composite and not a decode.
        if (project != nullptr && clip->activeSource().isValid()) {
            const model::MediaRef* media = project->findMedia(clip->activeSource());
            if (media != nullptr && media->info.isStill()) {
                return true;
            }
            // A fifth: a grade carried by the media file itself. That grade is
            // a second full primary -- balance, exposure, a CDL and contrast --
            // applied before the clip's own, and it does not fold into the
            // clip's: white balance is a channel multiply and contrast is a
            // power about middle grey, so "the file's, then the clip's" is not
            // any single correction this shader's uniforms could carry.
            //
            // Sent to the CPU graph rather than given uniforms of its own, for
            // the reason the four above give and one more that is specific to
            // this compositor: its uniform block is 512 bytes today, and on the
            // D3D11 backend here *any* other size -- even 528 -- renders the
            // Y'CbCr path as garbage, transforms included. Until that is
            // understood, growing the block is not a change that can be made
            // safely, and the CPU graph already produces exactly the right
            // answer and is what the export uses, so the preview and the
            // delivered file cannot disagree.
            if (media != nullptr && media->isGraded()) {
                return true;
            }
        }
    }
    return false;
}

/// This graph, as something `walkVideo` can drive.
///
/// Only the drawing. Track order, transition dispatch, the shape a transition
/// makes and how it folds into a clip's transform are core's, and the CPU graph
/// gets its answers from the same place.
class GpuRenderGraph::Sink final : public render::VideoSink {
public:
    Sink(GpuRenderGraph& graph, const model::Sequence& sequence)
        : graph_{graph}, sequence_{sequence} {}

    void draw(const model::Clip& clip, const model::Transform& transform,
              const time::RationalTime& at, int side, const model::Mask* wipe) override {
        // Both halves of a dissolve are resolved before either is composited,
        // so a generated picture needs a buffer per side. A nest does not --
        // `drawTransitionSide` composites one into a frame it owns.
        render::RgbaImage& scratch = side == 0 ? graph_.generated_ : graph_.generatedB_;
        if (graph_.drawTransitionSide(clip, sequence_, transform, at, scratch, wipe)) {
            ++graph_.lastClipCount_;
        }
    }

    void adjustment(const model::Clip& /*clip*/, const time::RationalTime& /*at*/) override {
        // Unreachable, and deliberately silent rather than asserting: a
        // sequence with an adjustment layer anywhere live never gets here,
        // because `needsCpuFallback` sends the whole frame down the CPU path
        // before the walk starts. If that guard is ever narrowed, the symptom
        // is an adjustment layer that does nothing on the preview and works on
        // export -- so the guard is the thing to change, not this.
    }

    void caption(const model::Graphic& graphic) override {
        if (graph_.text_ == nullptr) {
            return;
        }
        if (graph_.generated_.width() != sequence_.width() ||
            graph_.generated_.height() != sequence_.height()) {
            graph_.generated_ = render::RgbaImage{sequence_.width(), sequence_.height()};
        }
        if (render::drawText(graphic, graph_.text_, graph_.generated_)) {
            static_cast<void>(graph_.compositor_->draw(graph_.generated_, model::Transform{},
                                                       model::BlendMode::Normal));
        }
    }

private:
    GpuRenderGraph& graph_;
    const model::Sequence& sequence_;
};

Status GpuRenderGraph::drawClips(const model::Sequence& sequence, const time::RationalTime& at) {
    lastClipCount_ = 0;

    // Two things this compositor cannot do in its one queued pass: an
    // adjustment layer, which needs the accumulated frame read back and
    // corrected, and an effect, which needs to read a pixel's neighbours rather
    // than the one the sampler returned. Rather than restructure into ping-pong
    // passes and a per-clip pre-pass for cases that are rare in a preview and
    // never on the path that delivers, the whole frame is composited on the CPU
    // and uploaded.
    //
    // The result is not merely close to the export: it is the same code. The
    // cost is a slow frame wherever an adjustment layer is, and that is a
    // trade worth stating rather than hiding.
    if (render::RenderGraph* whole =
            needsCpuFallback(sequence, at, project_) ? cpuGraph() : nullptr;
        whole != nullptr) {
        auto frame = whole->composite(sequence, at);
        if (!frame) {
            return frame.error();
        }
        lastClipCount_ = whole->lastClipCount();
        return compositor_->draw(*frame, model::Transform{}, model::BlendMode::Normal);
    }

    // The same curve the CPU path reads, from the same place. A display curve
    // taken from different sources is the kind of disagreement that only shows
    // up in an export somebody has already signed off.
    transfer_ = sequence.output().transfer;

    // What is on this frame, and in what order, is decided by `walkVideo` --
    // the same walk the CPU graph runs. The two used to traverse the sequence
    // separately and had to be kept agreeing by hand; see core's VideoWalk.h
    // for the two bugs that cost.
    Sink sink{*this, sequence};
    render::walkVideo(sequence, at, sink);
    return {};
}

Status GpuRenderGraph::composite(const model::Sequence& sequence, const time::RationalTime& at) {
    if (Status begun = compositor_->beginFrame(sequence.width(), sequence.height()); !begun) {
        return begun;
    }
    if (Status drawn = drawClips(sequence, at); !drawn) {
        return drawn;
    }
    return compositor_->endFrameOnGpu();
}

Status GpuRenderGraph::compositeOn(::QRhiCommandBuffer* commandBuffer,
                                   const model::Sequence& sequence, const time::RationalTime& at) {
    if (Status begun =
            compositor_->beginFrameOn(commandBuffer, sequence.width(), sequence.height());
        !begun) {
        return begun;
    }
    if (Status drawn = drawClips(sequence, at); !drawn) {
        return drawn;
    }
    return compositor_->endFrameOnGpu();
}

Status GpuRenderGraph::compositeInto(const model::Sequence& sequence, const time::RationalTime& at,
                                     render::RgbaImage& out) {
    if (Status begun = compositor_->beginFrame(sequence.width(), sequence.height()); !begun) {
        return begun;
    }
    if (Status drawn = drawClips(sequence, at); !drawn) {
        return drawn;
    }
    return compositor_->endFrame(out);
}

}  // namespace zaro::platform::qrhi
