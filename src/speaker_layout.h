#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace spatial {
struct SpeakerDefinition { std::wstring name; double azimuthDegrees{},elevationDegrees{},trimDb{}; };
struct OutputRouteDefinition {
    std::wstring name,endpointFilter;
    std::vector<std::size_t> speakerIndices;
    double delayMilliseconds{};
    std::vector<long> physicalChannels;
    long asioBufferFrames{};
    double asioSampleRate{48000};
};
struct SpeakerLayout {
    std::wstring name;
    std::vector<SpeakerDefinition> speakers;
    std::vector<OutputRouteDefinition> outputs;
    std::size_t masterOutput{};
    std::optional<std::size_t> FindSpeaker(std::wstring_view name) const;
};
SpeakerLayout LoadSpeakerLayout(const std::filesystem::path& path);
std::vector<double> PanAdmOarPoint(const SpeakerLayout&,double x,double y,double z,double anchorTolerance=0);
}
