#include "zaro/core/io/OtioIo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace zaro::io {
namespace {

using json = nlohmann::json;

/// OTIO writes rates as doubles. 24000/1001 becomes 23.976023976023978, and
/// reading that back as a ratio gives something that is not the rate anybody
/// means -- so a rate that is within a hair of a broadcast standard is snapped
/// to the exact rational for it.
///
/// The alternative, keeping whatever ratio the double happens to approximate,
/// produces a sequence whose frames drift against every other tool's by a
/// fraction of a frame per hour. The snapping is deliberate and its list is
/// short: these are the rates that exist.
time::Rational rationalFromDouble(double value) {
    const time::Rational known[] = {
        time::rates::fps23_976, time::rates::fps24,     time::rates::fps25,  time::rates::fps29_97,
        time::rates::fps30,     time::rates::fps48,     time::rates::fps50,  time::rates::fps59_94,
        time::rates::fps60,     time::rates::fps119_88, time::rates::fps120, time::rates::hz44100,
        time::rates::hz48000,   time::rates::hz96000,
    };
    for (const time::Rational& rate : known) {
        if (std::fabs(rate.toDouble() - value) < 1e-6) {
            return rate;
        }
    }
    // Not a rate anybody standardised. An integer is exact; anything else is
    // approximated, and approximating is better than refusing a file over a
    // rate that may not even be used by any clip in it.
    if (std::fabs(value - std::round(value)) < 1e-9 && value > 0.0) {
        return time::Rational{static_cast<std::int64_t>(std::llround(value)), 1};
    }
    return time::Rational::approximate(value);
}

json writeTime(const time::RationalTime& value) {
    return json{{"OTIO_SCHEMA", "RationalTime.1"},
                {"rate", value.rate().toDouble()},
                {"value", static_cast<double>(value.frames())}};
}

json writeRange(const time::TimeRange& range) {
    return json{{"OTIO_SCHEMA", "TimeRange.1"},
                {"start_time", writeTime(range.start())},
                {"duration", writeTime(range.duration())}};
}

Result<time::RationalTime> readTime(const json& node, const char* what) {
    if (!node.is_object() || !node.contains("rate") || !node.contains("value")) {
        return Error{ErrorCode::InvalidData, std::string{what} + " is not an OTIO RationalTime"};
    }
    const time::Rational rate = rationalFromDouble(node.at("rate").get<double>());
    const double value = node.at("value").get<double>();
    return time::RationalTime{static_cast<std::int64_t>(std::llround(value)), rate};
}

Result<time::TimeRange> readRange(const json& node, const char* what) {
    if (!node.is_object() || !node.contains("start_time") || !node.contains("duration")) {
        return Error{ErrorCode::InvalidData, std::string{what} + " is not an OTIO TimeRange"};
    }
    auto start = readTime(node.at("start_time"), what);
    if (!start) {
        return start.error();
    }
    auto duration = readTime(node.at("duration"), what);
    if (!duration) {
        return duration.error();
    }
    return time::TimeRange{*start, *duration};
}

/// A span across a cut, as OTIO spells one.
///
/// An OTIO transition is not an item with a duration; it is a marker between
/// two items with an offset reaching either side of the join. That is exactly
/// this model's shape -- a span straddling a cut rather than an overlap -- so
/// the two need no translation beyond naming the halves.
json writeTransition(const model::Transition& span, const time::RationalTime& cut) {
    json out{{"OTIO_SCHEMA", "Transition.1"},
             {"name", model::toString(span.kind)},
             // OTIO names one blend and calls everything else custom. A
             // dissolve is the one it knows; the rest keep their own name in
             // metadata, so a round trip through this reader loses nothing and
             // every other tool sees a dissolve -- which is a cut somebody can
             // still watch, and the same fallback an unknown kind gets on load.
             {"transition_type",
              span.kind == model::TransitionKind::CrossDissolve ? "SMPTE_Dissolve" : "Custom"},
             {"in_offset", writeTime(cut - span.range.start())},
             {"out_offset", writeTime(span.range.endExclusive() - cut)}};

    json mine{{"kind", model::toString(span.kind)}, {"easing", model::toString(span.easing)}};
    // Written only where they mean something, the way the project format does
    // it: a dissolve has no direction to travel in and a slide has no edge.
    if (model::transitionTravels(span.kind)) {
        mine["direction"] = model::toString(span.direction);
    }
    if (model::transitionHasEdge(span.kind) && span.softness > 0.0) {
        mine["softness"] = span.softness;
    }
    out["metadata"] = json{{"zaro", std::move(mine)}};
    return out;
}

/// The schema family, without its version: "Clip.1" is a Clip.
std::string schemaName(const json& node) {
    if (!node.is_object() || !node.contains("OTIO_SCHEMA")) {
        return {};
    }
    const std::string full = node.at("OTIO_SCHEMA").get<std::string>();
    const std::size_t dot = full.rfind('.');
    return dot == std::string::npos ? full : full.substr(0, dot);
}

std::string urlForPath(const std::string& path) {
    return path.rfind("file://", 0) == 0 || path.find("://") != std::string::npos
               ? path
               : "file://" + path;
}

std::string pathForUrl(const std::string& url) {
    return url.rfind("file://", 0) == 0 ? url.substr(7) : url;
}

}  // namespace

Result<std::string> writeOtio(const model::Project& project, model::SequenceId sequenceId) {
    const model::Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr) {
        return Error{ErrorCode::NotFound, "no such sequence"};
    }

    json tracks = json::array();
    for (const model::TrackKind kind : {model::TrackKind::Video, model::TrackKind::Audio}) {
        const auto& list =
            kind == model::TrackKind::Video ? sequence->videoTracks() : sequence->audioTracks();
        for (const model::Track& track : list) {
            json children = json::array();
            time::RationalTime cursor{0, sequence->frameRate()};

            for (const model::Clip& clip : track.clips()) {
                // A hole is an object here, not an absence.
                if (clip.start() > cursor) {
                    children.push_back(
                        json{{"OTIO_SCHEMA", "Gap.1"},
                             {"name", "gap"},
                             {"source_range",
                              writeRange(time::TimeRange::fromStartEnd(cursor, clip.start()))}});
                }

                json item{{"OTIO_SCHEMA", "Clip.1"},
                          {"name", clip.name},
                          {"source_range", writeRange(clip.sourceRange)},
                          {"enabled", clip.enabled}};
                if (const model::MediaRef* media = project.findMedia(clip.source);
                    media != nullptr) {
                    item["media_reference"] = json{{"OTIO_SCHEMA", "ExternalReference.1"},
                                                   {"target_url", urlForPath(media->path)},
                                                   {"name", media->name}};
                } else if (clip.graphic.isSet()) {
                    // A generated clip has no media. `MissingReference` is
                    // OTIO's own way of saying so, and it survives a round trip
                    // through a tool that has never heard of a shape layer.
                    item["media_reference"] = json{{"OTIO_SCHEMA", "MissingReference.1"}};
                }
                children.push_back(std::move(item));
                cursor = clip.endExclusive();

                // A span leaving this clip goes between it and the one it
                // joins, which is where OTIO puts a transition and why this
                // sits inside the loop rather than after it.
                //
                // Cross fades only. A fade lies *inside* its clip and joins it
                // to nothing, while an OTIO transition is a join between two
                // items -- so writing one would be inventing the item on the
                // far side. Left out rather than guessed at, which is the same
                // call the reader makes about a Stack.
                for (const model::Transition& span : track.transitions()) {
                    if (span.from == clip.id && span.isCrossFade()) {
                        children.push_back(writeTransition(span, clip.endExclusive()));
                        break;
                    }
                }
            }

            tracks.push_back(json{{"OTIO_SCHEMA", "Track.1"},
                                  {"name", track.name()},
                                  {"kind", kind == model::TrackKind::Video ? "Video" : "Audio"},
                                  {"enabled", !track.isMuted()},
                                  {"children", std::move(children)}});
        }
    }

    json timeline{
        {"OTIO_SCHEMA", "Timeline.1"},
        {"name", sequence->name()},
        // Rescaled into the sequence's own rate, always. OTIO states the
        // timeline's rate nowhere else, so this is where a reader gets it --
        // and a start time carrying the rate it happened to be constructed
        // with would hand every reader the wrong one.
        {"global_start_time",
         writeTime(sequence->startTime().rate().isPositive()
                       ? sequence->startTime().rescaledTo(sequence->frameRate())
                       : time::RationalTime{0, sequence->frameRate()})},
        {"tracks",
         json{{"OTIO_SCHEMA", "Stack.1"}, {"name", "tracks"}, {"children", std::move(tracks)}}}};
    return timeline.dump(2);
}

Result<model::Project> readOtio(const std::string& text) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error& error) {
        return Error{ErrorCode::InvalidData, std::string{"not JSON: "} + error.what()};
    }
    if (schemaName(root) != "Timeline") {
        return Error{ErrorCode::InvalidData, "the file is not an OTIO Timeline"};
    }

    model::Project project;
    const json& stack = root.value("tracks", json::object());
    if (schemaName(stack) != "Stack") {
        return Error{ErrorCode::InvalidData, "the timeline has no track stack"};
    }

    // The timeline's own rate is not stated: OTIO puts it on the times. The
    // first one found wins, and 25 stands in for a timeline with nothing in it
    // at all.
    time::Rational rate = time::rates::fps25;
    bool haveRate = false;
    if (root.contains("global_start_time")) {
        if (auto start = readTime(root.at("global_start_time"), "global_start_time");
            start && start->rate().isPositive()) {
            rate = start->rate();
            haveRate = true;
        }
    }
    if (!haveRate) {
        for (const json& track : stack.value("children", json::array())) {
            for (const json& child : track.value("children", json::array())) {
                if (child.contains("source_range")) {
                    if (auto range = readRange(child.at("source_range"), "source_range")) {
                        rate = range->start().rate();
                        haveRate = true;
                        break;
                    }
                }
            }
            if (haveRate) {
                break;
            }
        }
    }

    model::Sequence sequence{project.ids().next<model::SequenceTag>(),
                             root.value("name", std::string{"Timeline"}), rate};
    if (root.contains("global_start_time")) {
        if (auto start = readTime(root.at("global_start_time"), "global_start_time")) {
            sequence.setStartTime(*start);
        }
    }

    std::map<std::string, model::MediaRefId> byUrl;
    for (const json& trackNode : stack.value("children", json::array())) {
        if (schemaName(trackNode) != "Track") {
            continue;
        }
        const std::string kindText = trackNode.value("kind", std::string{"Video"});
        const model::TrackKind kind =
            kindText == "Audio" ? model::TrackKind::Audio : model::TrackKind::Video;
        const auto trackId = project.ids().next<model::TrackTag>();
        sequence.addTrack(trackId, kind, trackNode.value("name", std::string{"Track"}));
        model::Track* track = sequence.findTrack(trackId);
        track->setMuted(!trackNode.value("enabled", true));

        // Position is implied by order and duration; this is where it comes
        // back.
        time::RationalTime cursor{0, rate};
        // A transition names the clips either side of it, and the one after it
        // has not been read yet -- so what the file says is collected here and
        // turned into spans once the whole track exists.
        struct PendingSpan {
            time::RationalTime cut;
            time::RationalTime in;
            time::RationalTime out;
            model::TransitionKind kind{model::TransitionKind::CrossDissolve};
            model::TransitionDirection direction{model::TransitionDirection::Right};
            double softness{0.0};
            model::TransitionEasing easing{model::TransitionEasing::Linear};
        };
        std::vector<PendingSpan> pending;

        for (const json& child : trackNode.value("children", json::array())) {
            const std::string what = schemaName(child);
            // Before the source_range check below, because a transition has
            // none: it is a marker between two items rather than an item with
            // a duration, and it consumes no timeline time. That is also why
            // this does not advance the cursor.
            if (what == "Transition") {
                PendingSpan span;
                span.cut = cursor;
                if (child.contains("in_offset")) {
                    auto offset = readTime(child.at("in_offset"), "in_offset");
                    if (!offset) {
                        return offset.error();
                    }
                    span.in = offset->rescaledTo(rate);
                }
                if (child.contains("out_offset")) {
                    auto offset = readTime(child.at("out_offset"), "out_offset");
                    if (!offset) {
                        return offset.error();
                    }
                    span.out = offset->rescaledTo(rate);
                }
                // Anything this writer wrote comes back exactly; anything else
                // is a dissolve of the length the offsets give, which is what
                // every tool that does not know our kinds will have meant.
                const json meta = child.value("metadata", json::object());
                const json mine =
                    meta.is_object() ? meta.value("zaro", json::object()) : json::object();
                if (mine.is_object()) {
                    if (mine.contains("kind")) {
                        span.kind = model::transitionKindFromString(
                            mine.at("kind").get<std::string>().c_str());
                    }
                    if (mine.contains("direction")) {
                        model::TransitionDirection direction{};
                        if (model::transitionDirectionFromString(
                                mine.at("direction").get<std::string>().c_str(), direction)) {
                            span.direction = direction;
                        }
                    }
                    if (mine.contains("easing")) {
                        span.easing = model::transitionEasingFromString(
                            mine.at("easing").get<std::string>().c_str());
                    }
                    span.softness = std::clamp(mine.value("softness", 0.0), 0.0, 1.0);
                }
                pending.push_back(span);
                continue;
            }
            if (!child.contains("source_range")) {
                continue;
            }
            auto range = readRange(child.at("source_range"), "source_range");
            if (!range) {
                return range.error();
            }
            const time::RationalTime duration = range->duration().rescaledTo(rate);
            if (what == "Gap") {
                cursor = cursor + duration;
                continue;
            }
            if (what != "Clip") {
                // A Stack or a Transition inside a track. Skipped rather than
                // guessed at, and its duration still advances the cursor so
                // everything after it stays where the file put it.
                cursor = cursor + duration;
                continue;
            }

            model::Clip clip;
            clip.id = project.ids().next<model::ClipTag>();
            clip.name = child.value("name", std::string{});
            clip.enabled = child.value("enabled", true);
            clip.sourceRange = *range;
            clip.timelineRange = time::TimeRange{cursor, duration};

            const json& reference = child.value("media_reference", json::object());
            if (schemaName(reference) == "ExternalReference") {
                const std::string url = reference.value("target_url", std::string{});
                auto found = byUrl.find(url);
                if (found == byUrl.end()) {
                    model::MediaRef media;
                    media.id = project.ids().next<model::MediaRefTag>();
                    media.path = pathForUrl(url);
                    media.name = reference.value("name", std::string{});
                    if (media.name.empty()) {
                        const std::size_t slash = media.path.rfind('/');
                        media.name =
                            slash == std::string::npos ? media.path : media.path.substr(slash + 1);
                    }
                    found = byUrl.emplace(url, project.addMedia(std::move(media))).first;
                }
                clip.source = found->second;
            }

            track->insert(clip);
            cursor = cursor + duration;
        }

        // Now that the whole track exists, each span can name the clips it
        // joins. One that lands where two clips do not actually meet is
        // dropped: the file said a join and there is not one, and a transition
        // pointing at a clip that is not there is worse than none.
        std::vector<model::Transition> spans;
        for (const PendingSpan& span : pending) {
            const model::Clip* incoming = track->clipAt(span.cut);
            const model::Clip* outgoing = nullptr;
            for (const model::Clip& candidate : track->clips()) {
                if (candidate.endExclusive() == span.cut) {
                    outgoing = &candidate;
                }
            }
            if (outgoing == nullptr || incoming == nullptr) {
                continue;
            }
            model::Transition made;
            made.id = project.ids().next<model::TransitionTag>();
            made.from = outgoing->id;
            made.to = incoming->id;
            made.range = time::TimeRange::fromStartEnd(span.cut - span.in, span.cut + span.out);
            made.kind = span.kind;
            made.direction = span.direction;
            made.softness = span.softness;
            made.easing = span.easing;
            if (made.range.duration().frames() > 0) {
                spans.push_back(made);
            }
        }
        if (!spans.empty()) {
            track->setTransitions(std::move(spans));
        }
    }

    project.addSequence(std::move(sequence));
    return project;
}

Status saveOtio(const model::Project& project, model::SequenceId sequenceId,
                const std::string& path) {
    auto text = writeOtio(project, sequenceId);
    if (!text) {
        return text.error();
    }
    std::ofstream file{path, std::ios::binary};
    if (!file) {
        return Error{ErrorCode::Io, "cannot write " + path};
    }
    file.write(text->data(), static_cast<std::streamsize>(text->size()));
    if (!file) {
        return Error{ErrorCode::Io, "cannot write " + path};
    }
    return {};
}

Result<model::Project> loadOtio(const std::string& path) {
    std::ifstream file{path};
    if (!file) {
        return Error{ErrorCode::NotFound, "cannot open " + path};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return readOtio(buffer.str());
}

}  // namespace zaro::io
