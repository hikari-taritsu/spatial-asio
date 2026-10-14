#pragma once
#include "audio_queue.h"
#include <memory>

namespace spatial {
struct AsioDriverDescriptor { std::wstring id, name; };
std::vector<AsioDriverDescriptor> EnumerateAsioDrivers();
void PrintAsioDriversJson();
void ProbeAsioDriverJson(const std::wstring& id);
void ShowAsioControlPanel(const std::wstring& id);
bool IsAsioEndpoint(std::wstring_view id);

class AsioRenderer {
public:
    AsioRenderer(const SpeakerLayout& layout, double gain, RendererLatencyMode latency);
    ~AsioRenderer();
    void Prime(const InterleavedPcmQueue& queue);
    void Start();
    void Service(const InterleavedPcmQueue& queue, bool countStarvation, DWORD timeout);
    void DiscardConsumed(InterleavedPcmQueue& queue) const;
    std::uint64_t MinimumFramesAvailable(const InterleavedPcmQueue& queue) const;
    std::uint32_t MaximumBufferFrames() const;
    bool IsDrained(const InterleavedPcmQueue& queue) const;
    bool IsStarted() const;
    void Stop();
    void Pause();
    void SetGain(double gain); // Worker thread only; short ramp in producer.
    std::vector<EndpointRenderStats> Stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
