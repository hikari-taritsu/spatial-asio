#pragma once
#include "speaker_layout.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace spatial {
enum class RendererLatencyMode { Safe,Balanced,Low };
struct EndpointRenderStats {
    std::wstring routeName,endpointName;
    bool isMaster{},lowLatencyApi{},mmcssEnabled{};
    std::uint64_t starvationFrames{};
    double clockSeconds{},clockDeltaMilliseconds{},relativeRatePpm{},resampleAdjustmentPpm{},phaseErrorMilliseconds{},maximumPhaseErrorMilliseconds{},bufferMilliseconds{},streamLatencyMilliseconds{};
    std::uint32_t bufferFrames{},defaultPeriodFrames{},minimumPeriodFrames{},selectedPeriodFrames{};
};
// Decode-worker queue only. Hardware callbacks use AsioPcmRing instead.
class InterleavedPcmQueue {
    std::size_t channels_; std::uint64_t base_{}; std::vector<float> data_;
public:
    explicit InterleavedPcmQueue(std::size_t channels):channels_(channels) { if(!channels || channels>32) throw std::invalid_argument("Channel count"); }
    std::size_t Channels() const { return channels_; }
    std::uint64_t EndFrame() const { return base_+data_.size()/channels_; }
    std::uint64_t FramesAvailable(double from) const { return from>=static_cast<double>(EndFrame()) ? 0 : static_cast<std::uint64_t>(static_cast<double>(EndFrame())-std::max(0.,std::floor(from))); }
    double Sample(std::uint64_t f,std::size_t c) const { return f<base_ || f>=EndFrame() || c>=channels_ ? 0 : data_[(f-base_)*channels_+c]; }
    void AppendFloat(std::vector<float>&& block) {
        if(block.size()%channels_ || data_.size()+block.size()>channels_*192000) throw std::runtime_error("Invalid or stalled PCM queue");
        for(auto v:block) if(!std::isfinite(v)) throw std::runtime_error("Nonfinite output sample");
        data_.insert(data_.end(),block.begin(),block.end());
    }
    void Append(std::vector<std::int16_t>&& block) { std::vector<float> v;v.reserve(block.size());for(auto x:block)v.push_back(x/32768.f);AppendFloat(std::move(v)); }
    void DiscardBefore(double frame) {
        const auto until=std::clamp(frame,static_cast<double>(base_),static_cast<double>(EndFrame()));
        const auto count=static_cast<std::uint64_t>(until)-base_;
        // Amortize erases; retain enough history for the interpolating producer.
        if(count>=4096) { data_.erase(data_.begin(),data_.begin()+count*channels_); base_+=count; }
    }
};
}
