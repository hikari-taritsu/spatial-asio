#pragma once
#include "speaker_layout.h"
#include "metadata_renderer.h"
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace spatial {
// NSP1 is a local, versioned transport. The payload retains the original properties.
// All fields are little endian; Windows x64 is the only supported capture platform.
struct NativePacketHeader {
    std::uint32_t magic{0x3150534e}, bytes{};
    std::uint64_t stream{}, sequence{}, captureNs{};
    std::uint32_t sampleRate{48000}, objects{}, frames{}, flags{};
};
static_assert(sizeof(NativePacketHeader) == 48);
struct NativeProperty {
    std::uint32_t candidateId{}, frames{}, type{}, flags{};
    float x{}, y{}, z{}, gain{};
};
static_assert(sizeof(NativeProperty) == 32);
struct NativePacket {
    NativePacketHeader header;
    std::vector<NativeProperty> properties;
    std::vector<float> planar;
};
void ValidateNativeHeader(const NativePacketHeader& header);
NativePacket DecodeNativePacket(const NativePacketHeader& header, std::span<const std::uint8_t> bytes);
struct NativeSpatialStats {
    std::uint64_t packets{}, frames{}, creates{}, retires{}, updates{}, gaps{};
    std::uint64_t unknownStateBlocks{}, unknownTypes{}, unknownFlagBlocks{}, nonzeroDynamicBlocks{};
    std::uint64_t lateFrames{}, silentFrames{}, clippedSamples{}, expiredStreams{};
};
class NativeSpatialScene {
public:
    explicit NativeSpatialScene(const SpeakerLayout& layout);
    std::vector<float> Render(const NativePacket& packet);
    std::vector<float> FlushStream(std::uint64_t stream);
    void RetireStream(std::uint64_t stream);
    void Reset();
    const NativeSpatialStats& Stats() const { return stats_; }
private:
    struct Slot {
        std::uint32_t candidateId{}, type{};
        bool positionKnown{}, gainKnown{}, renderStarted{};
        float x{}, y{}, z{}, gain{};
    };
    struct Stream {
        bool seen{};
        std::uint64_t sequence{};
        std::map<std::size_t, Slot> slots;
        std::unique_ptr<MetadataRenderer> renderer;
    };
    SpeakerLayout layout_;
    std::map<std::uint64_t, Stream> streams_;
    NativeSpatialStats stats_;
};
// QPC aligns the start of streams approximately; each stream thereafter uses its
// own frame count. This does not claim sample-accurate clock alignment across apps.
class NativeSpatialTimeline {
public:
    explicit NativeSpatialTimeline(const SpeakerLayout& layout);
    void Add(const NativePacket& packet);
    std::vector<float> Take(std::uint64_t untilFrame);
    std::uint64_t EndFrame() const { return end_; }
    std::uint64_t Cursor() const { return cursor_; }
    std::uint64_t AnchorNs() const { return anchor_; }
    void Finish() { scene_.Reset(); }
    void Complete(); // Drain the lookahead exactly once at producer EOF.
    NativeSpatialStats Stats() const;
private:
    struct Timing { std::uint64_t next{}, sequence{}, captureNs{}; std::uint32_t frames{}; bool sealed{}; };
    void DrainStream(std::uint64_t id, Timing& timing);
    std::size_t channels_;
    NativeSpatialScene scene_;
    std::map<std::uint64_t, Timing> streams_;
    // Larger than the bounded 96k-frame window plus one maximum packet/tail.
    static constexpr std::size_t capacity_=131072;
    std::vector<float> pending_;
    std::vector<std::uint64_t> stamps_;
    float* Row(std::uint64_t frame);
    std::uint64_t anchor_{}, cursor_{}, end_{}, late_{}, silent_{}, expired_{};
    bool completed_{};
};
}
