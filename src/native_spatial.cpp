#include "native_spatial.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>

namespace spatial {
void ValidateNativeHeader(const NativePacketHeader& h) {
    if (h.magic != 0x3150534e || h.flags != 0 || h.sampleRate != 48000 ||
        h.objects < 1 || h.objects > 160 || h.frames < 1 || h.frames > 4096 ||
        h.bytes != h.objects * (32 + h.frames * 4) || !h.captureNs)
        throw std::runtime_error("Invalid NSP1 header; only 48 kHz float32 basic objects are supported");
}
NativePacket DecodeNativePacket(const NativePacketHeader& h, std::span<const std::uint8_t> data) {
    ValidateNativeHeader(h);
    if (data.size() != h.bytes) throw std::runtime_error("Truncated NSP1 payload");
    NativePacket p{h};
    p.properties.resize(h.objects);
    p.planar.resize(static_cast<std::size_t>(h.objects) * h.frames);
    std::memcpy(p.properties.data(), data.data(), h.objects * 32);
    std::memcpy(p.planar.data(), data.data() + h.objects * 32, p.planar.size() * 4);
    for (const auto& v : p.properties) {
        if (v.frames != h.frames || !std::isfinite(v.x) || !std::isfinite(v.y) ||
            !std::isfinite(v.z) || !std::isfinite(v.gain) || v.gain < 0)
            throw std::runtime_error("Invalid native object property");
    }
    if (std::any_of(p.planar.begin(), p.planar.end(), [](float v) { return !std::isfinite(v); }))
        throw std::runtime_error("Nonfinite native object PCM");
    return p;
}
NativeSpatialScene::NativeSpatialScene(const SpeakerLayout& layout) : layout_(layout) {

}
void NativeSpatialScene::RetireStream(std::uint64_t stream) {
    if (auto it = streams_.find(stream); it != streams_.end()) {
        stats_.retires += it->second.slots.size(); streams_.erase(it);
    }
}
void NativeSpatialScene::Reset() {
    for (const auto& [id,s] : streams_) stats_.retires += s.slots.size();
    streams_.clear();
}
std::vector<float> NativeSpatialScene::Render(const NativePacket& p) {
    ValidateNativeHeader(p.header);
    if (p.properties.size() != p.header.objects || p.planar.size() != p.header.objects * p.header.frames)
        throw std::runtime_error("Native packet dimensions do not match its header");
    if (!streams_.contains(p.header.stream) && streams_.size() >= 16)
        throw std::runtime_error("Native spatial stream limit (16) exceeded");
    auto& stream = streams_[p.header.stream];
    if (stream.seen && p.header.sequence != stream.sequence + 1) {
        ++stats_.gaps; stats_.retires += stream.slots.size(); stream.slots.clear();
        stream.renderer.reset();
    }
    if (!stream.renderer) stream.renderer=std::make_unique<MetadataRenderer>(layout_,48000,160);
    std::vector<MetadataEvent> events;
    std::vector<float> interleaved(static_cast<std::size_t>(p.header.frames)*160);
    stream.seen = true; stream.sequence = p.header.sequence;
    std::vector<float> mix(static_cast<std::size_t>(p.header.frames) * layout_.speakers.size());
    for (std::size_t i = 0; i < p.properties.size(); ++i) {
        const auto& v = p.properties[i];
        auto it = stream.slots.find(i);
        // Identity is local to this stream and slot. Unknown flag bits never imply EOS.
        if (it != stream.slots.end() && (it->second.candidateId != v.candidateId || it->second.type != v.type)) {
            events.push_back({0,static_cast<unsigned>(i),MetadataEnd});
            stream.slots.erase(it); ++stats_.retires;
        }
        auto [pos, created] = stream.slots.try_emplace(i);
        auto& s = pos->second;
        if (created) { s.candidateId = v.candidateId; s.type = v.type; ++stats_.creates; }
        if (v.flags & ~6U) ++stats_.unknownFlagBlocks;
        if (v.flags & 2) { s.gain = v.gain; s.gainKnown = true; ++stats_.updates; }
        if (v.type == 1 && (v.flags & 4)) {
            s.x=v.x; s.y=v.y; s.z=v.z; s.positionKnown=true;
            ++stats_.updates;
        }
        constexpr std::uint32_t knownTypes[]{1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768,65536,131072,262144,524288};
        if (std::find(std::begin(knownTypes),std::end(knownTypes),v.type)==std::end(knownTypes)) { ++stats_.unknownTypes; continue; }
        if (!s.gainKnown || (v.type == 1 && !s.positionKnown)) { ++stats_.unknownStateBlocks; continue; }
        {
            constexpr std::uint32_t bedTypes[]{2,4,8,16,128,256,32,64,0,0,512,1024,0,0,2048,4096};
            int bed=-1;
            for (int c=0;c<16;++c) if (bedTypes[c]==v.type) bed=c;
            if (v.type==8192 || v.type==262144) bed=0;
            if (v.type==16384 || v.type==524288) bed=1;
            if (v.type==32768) bed=4;
            if (v.type==65536) bed=5;
            const unsigned flags=MetadataGain | MetadataPosition | MetadataBed | (s.renderStarted ? 0U : MetadataBegin);
            // Native coordinates are metres, X right/Y up/Z rear. The point
            // renderer uses the unit room, with out-of-room values clamped.
            events.push_back({0,static_cast<unsigned>(i),flags,bed,
                std::clamp(s.x,-1.f,1.f),v.type==131072 ? -1.f : std::clamp(-s.z,-1.f,1.f),
                std::clamp(s.y,0.f,1.f),s.gain,0});
            s.renderStarted=true;
        }
        bool nonzero = false;
        for (std::size_t f = 0; f < p.header.frames; ++f) {
            const float sample = p.planar[i * p.header.frames + f];
            nonzero |= sample != 0;
            interleaved[f*160+i]=sample;
        }
        if (nonzero && v.type == 1) ++stats_.nonzeroDynamicBlocks;
    }
    for (auto it = stream.slots.lower_bound(p.properties.size()); it != stream.slots.end();) {
        events.push_back({0,static_cast<unsigned>(it->first),MetadataEnd});
        ++stats_.retires; it = stream.slots.erase(it);
    }
    stream.renderer->Process(interleaved,p.header.frames,events,mix);
    for (float sample : mix) {
        if (!std::isfinite(sample)) throw std::runtime_error("Native spatial mix overflow");
        if (std::abs(sample) > 1) ++stats_.clippedSamples; // Headroom warning, no pre-gain hard clipping.
    }
    ++stats_.packets; stats_.frames += p.header.frames;
    return mix;
}
std::vector<float> NativeSpatialScene::FlushStream(std::uint64_t id) {
    auto it=streams_.find(id);
    if (it==streams_.end() || !it->second.renderer) return {};
    auto& renderer=*it->second.renderer;
    std::vector<float> result(renderer.Period()*layout_.speakers.size());
    result.resize(renderer.Flush(result)*layout_.speakers.size());
    return result;
}
NativeSpatialTimeline::NativeSpatialTimeline(const SpeakerLayout& layout)
    : channels_(layout.speakers.size()), scene_(layout),
      pending_(capacity_*channels_), stamps_(capacity_) {}
float* NativeSpatialTimeline::Row(std::uint64_t frame) {
    if(frame<cursor_ || frame-cursor_>=capacity_) throw std::runtime_error("Native timeline ring window exceeded");
    const auto index=frame%capacity_;
    auto* row=pending_.data()+index*channels_;
    if(stamps_[index]!=frame+1) { std::fill_n(row,channels_,0.f);stamps_[index]=frame+1; }
    return row;
}
void NativeSpatialTimeline::Add(const NativePacket& p) {
    if (completed_) throw std::runtime_error("Cannot add packets after native timeline completion");
    if (!anchor_) anchor_ = p.header.captureNs;
    for (auto it = streams_.begin(); it != streams_.end();) {
        if (p.header.captureNs > it->second.captureNs + 2'000'000'000ULL) {
            DrainStream(it->first,it->second);
            scene_.RetireStream(it->first); it = streams_.erase(it); ++expired_;
        } else ++it;
    }
    const auto& h = p.header;
    auto it = streams_.find(h.stream);
    if (it != streams_.end() && h.sequence <= it->second.sequence)
        throw std::runtime_error("Duplicate or backwards native spatial packet sequence");
    std::uint64_t start = 0;
    if (it != streams_.end() && !it->second.sealed && h.sequence == it->second.sequence + 1 &&
        h.captureNs >= it->second.captureNs && h.captureNs - it->second.captureNs < 250'000'000) {
        start = it->second.next;
    } else {
        // Do not reuse stale state after a restarted stream or large capture pause.
        if (it != streams_.end()) {
            DrainStream(h.stream,it->second);
            scene_.RetireStream(h.stream);
        }
        const double delta = h.captureNs >= anchor_ ? (h.captureNs - anchor_) * 48000.0 / 1e9 : 0;
        start = static_cast<std::uint64_t>(std::llround(delta / h.frames)) * h.frames;
    }
    if (start > cursor_ + 96000) throw std::runtime_error("Native capture timeline exceeds bounded two-second window");
    auto audio = scene_.Render(p);
    streams_[h.stream] = {start + h.frames, h.sequence, h.captureNs, h.frames};
    const auto skip = start < cursor_ ? std::min<std::uint64_t>(h.frames, cursor_ - start) : 0;
    late_ += skip;
    // Fixed storage: no per-frame allocation; preserve packet and sample addition order.
    for (std::size_t f = static_cast<std::size_t>(skip); f < h.frames; ++f) {
        auto* row=Row(start+f);
        for (std::size_t c = 0; c < channels_; ++c) row[c] += audio[f * channels_ + c];
    }
    end_ = std::max(end_, start + h.frames);
}
void NativeSpatialTimeline::Complete() {
    if (completed_) return;
    completed_=true;
    for (auto& [id,timing]:streams_) DrainStream(id,timing);
}
void NativeSpatialTimeline::DrainStream(std::uint64_t id, Timing& timing) {
    if (timing.sealed) return;
    const auto tail=scene_.FlushStream(id);
    for (std::size_t f=0;f<tail.size()/channels_;++f) {
        const auto at=timing.next+f;
        if (at<cursor_) { ++late_; continue; }
        auto* row=Row(at);
        for (std::size_t c=0;c<channels_;++c) row[c]+=tail[f*channels_+c];
    }
    end_=std::max(end_,timing.next+tail.size()/channels_);
    timing.sealed=true;
    scene_.RetireStream(id);
}
std::vector<float> NativeSpatialTimeline::Take(std::uint64_t until) {
    if (until < cursor_ || until > cursor_ + 96000) throw std::runtime_error("Invalid timeline read window");
    // The jitter-buffer deadline has passed this producer's last PCM frame.
    // Drain its lookahead before consuming silence; a later packet starts a new
    // timeline segment and must supply fresh metadata, as after a capture gap.
    for (auto& [id,timing]:streams_) if (until>timing.next) DrainStream(id,timing);
    std::vector<float> output(static_cast<std::size_t>(until - cursor_) * channels_);
    for (auto f = cursor_; f < until; ++f) {
        const auto index=f%capacity_;
        if (stamps_[index]!=f+1) { ++silent_; continue; }
        std::copy_n(pending_.data()+index*channels_,channels_,output.data()+(f-cursor_)*channels_);
        stamps_[index]=0;
    }
    cursor_ = until;
    return output;
}
NativeSpatialStats NativeSpatialTimeline::Stats() const {
    auto s = scene_.Stats(); s.lateFrames = late_; s.silentFrames = silent_; s.expiredStreams = expired_; return s;
}
}
