#include "metadata_renderer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace spatial {
SpeakerLayout Canonical916Layout() {
    SpeakerLayout layout;
    layout.name=L"Canonical 9.1.6";
    layout.speakers={{L"FL",-30,0,0},{L"FR",30,0,0},{L"FC",0,0,0},{L"LFE",0,0,0},
        {L"BL",-150,0,0},{L"BR",150,0,0},{L"SL",-90,0,0},{L"SR",90,0,0},
        {L"FWL",-60,0,0},{L"FWR",60,0,0},{L"TFL",-45,45,0},{L"TFR",45,45,0},
        {L"TML",-90,90,0},{L"TMR",90,90,0},{L"TBL",-135,45,0},{L"TBR",135,45,0}};
    return layout;
}
std::vector<std::vector<double>> BuildCanonical916Mix(const SpeakerLayout& layout) {
    if (layout.speakers.size()<2 || layout.speakers.size()>32)
        throw std::invalid_argument("Metadata renderer requires 2..32 output speakers");
    std::vector<std::vector<double>> mix;
    for (const auto& speaker : Canonical916Layout().speakers) {
        std::vector<double> row(layout.speakers.size());
        if (auto index=layout.FindSpeaker(speaker.name)) row[*index]=1;
        else if (speaker.name!=L"LFE") {
            // Missing wide/height channels fold into explicitly named neighboring channels.
            const bool left=speaker.name.ends_with(L"L") || speaker.name==L"FL" || speaker.name==L"BL" || speaker.name==L"SL";
            std::vector<std::wstring> targets;
            if (speaker.name==L"FWL") targets={L"FL",L"SL"};
            else if (speaker.name==L"FWR") targets={L"FR",L"SR"};
            else if (speaker.name==L"TML") targets={L"TFL",L"TBL"};
            else if (speaker.name==L"TMR") targets={L"TFR",L"TBR"};
            else if (speaker.name==L"FC") targets={L"FL",L"FR"};
            else if (speaker.name==L"BL") targets={L"SL"};
            else if (speaker.name==L"BR") targets={L"SR"};
            else if (speaker.name==L"TFL" || speaker.name==L"TBL") targets={L"TML"};
            else if (speaker.name==L"TFR" || speaker.name==L"TBR") targets={L"TMR"};
            std::vector<std::size_t> found;
            for (const auto& name:targets) if (auto i=layout.FindSpeaker(name)) found.push_back(*i);
            if (found.empty()) {
                const bool rear=speaker.name==L"TBL" || speaker.name==L"TBR";
                if(rear) if(auto i=layout.FindSpeaker(left ? L"SL" : L"SR")) found.push_back(*i);
                if(found.empty()) if (auto i=layout.FindSpeaker(left ? L"FL" : L"FR")) found.push_back(*i);
            }
            for (auto i:found) row[i]=1/std::sqrt(static_cast<double>(found.size()));
        }
        mix.push_back(std::move(row));
    }
    return mix;
}
MetadataRenderer::MetadataRenderer(const SpeakerLayout& layout, unsigned rate, unsigned objects)
    : canonical_(Canonical916Layout()), projection_(BuildCanonical916Mix(layout)),
      period_(rate==48000 ? 512 : 1024), channels_(static_cast<unsigned>(layout.speakers.size())), objects_(objects) {
    if ((rate!=48000 && rate!=96000) || objects<1 || objects>160)
        throw std::invalid_argument("Metadata renderer supports 48/96 kHz and 1..160 sources");
    const float exponent=-(static_cast<float>(period_)/(static_cast<float>(rate)*.025f));
    oldCoefficient_=static_cast<float>(std::exp(static_cast<double>(exponent)));
    newCoefficient_=1.f-oldCoefficient_;
    lanes_.resize(objects_);
    for (auto& lane:lanes_) lane.frames.resize(period_);
    ready_.resize(period_*channels_);
    canonicalMix_.resize(period_*16);
}
void MetadataRenderer::Reset() {
    position_=0; fill_=0; finished_=false;
    std::fill(ready_.begin(),ready_.end(),0.f);
    for (auto& lane:lanes_) {
        auto storage=std::move(lane.frames);
        lane=Lane{};
        lane.frames=std::move(storage);
    }
}
double MetadataRenderer::Amplitude(const Lane& lane) const {
    const double alpha=lane.ramp ? std::min(1.0,static_cast<double>(position_-lane.gainStart)/lane.ramp) : 1.0;
    return lane.initial+(lane.target-lane.initial)*alpha;
}
void MetadataRenderer::Apply(const MetadataEvent& e) {
    auto& lane=lanes_[e.object];
    const bool begin=(e.flags&MetadataBegin)!=0;
    if (begin) {
        lane.live=true; ++lane.generation;
        lane.position={0,1,0}; lane.bed=-1; lane.inactive=false;
        lane.initial=lane.target=1; lane.ramp=0; lane.gainStart=position_;
    }
    if (e.flags&MetadataPosition) lane.position={e.x,e.y,e.z};
    if (e.flags&MetadataBed) lane.bed=e.bed;
    if (e.flags&MetadataGain) {
        const bool inactive=(e.flags&MetadataInactive)!=0;
        lane.initial=Amplitude(lane);
        lane.target=e.gain;
        lane.ramp=(begin || inactive || lane.inactive) ? 0 : e.rampSamples;
        lane.gainStart=position_;
        lane.inactive=inactive;
    }
    if (e.flags&MetadataEnd) lane.live=false;
}
void MetadataRenderer::Process(std::span<const float> input, unsigned frames,
    std::span<const MetadataEvent> events, std::span<float> output) {
    if (finished_ || frames>65536 || input.size()!=static_cast<std::size_t>(frames)*objects_ ||
        output.size()!=static_cast<std::size_t>(frames)*channels_ || events.size()>1048576)
        throw std::invalid_argument("Invalid metadata stream block dimensions or finished stream");
    unsigned previous=0;
    for (const auto& e:events) {
        if (e.offset>frames || e.offset<previous || e.object>=objects_ || (e.flags&~63U) ||
            ((e.flags&MetadataPosition) && (!std::isfinite(e.x) || !std::isfinite(e.y) ||
                !std::isfinite(e.z) || std::abs(e.x)>1 || std::abs(e.y)>1 || e.z<0 || e.z>1)) ||
            ((e.flags&MetadataGain) && (!std::isfinite(e.gain) || e.gain<0)) ||
            ((e.flags&MetadataBed) && (e.bed < -1 || e.bed>=16)))
            throw std::invalid_argument("Invalid metadata event or unsorted event timestamps");
        previous=e.offset;
    }
    for (float value:input) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite object PCM");
    std::size_t event=0;
    for (unsigned frame=0; frame<frames; ++frame) {
        while (event<events.size() && events[event].offset==frame) Apply(events[event++]);
        std::copy_n(ready_.data()+fill_*channels_,channels_,output.data()+frame*channels_);
        for (unsigned object=0;object<objects_;++object) {
            auto& lane=lanes_[object];
            auto& value=lane.frames[fill_];
            value.generation=lane.live ? lane.generation : 0;
            value.position=lane.position; value.bed=lane.bed;
            value.pcm=lane.live ? input[frame*objects_+object]*static_cast<float>(Amplitude(lane)) : 0.f;
        }
        ++position_;
        if (++fill_==period_) { RenderPeriod(period_); fill_=0; }
    }
    while (event<events.size()) Apply(events[event++]); // zero-duration boundary events
}
void MetadataRenderer::GainBlock(Lane& lane, const std::array<float,16>& target, bool changed) {
    for (unsigned c=0;c<16;++c) {
        const bool skip=std::abs(target[c])<1e-6f && std::abs(lane.old[c])<1e-6f;
        const float diff=target[c]-lane.old[c];
        const bool snap=std::abs(diff)<1e-4f && !skip;
        if (snap) { lane.remaining[c]=0; lane.old[c]=target[c]; }
        if (changed && !snap && !skip) {
            lane.remaining[c]=period_/32;
            lane.delta[c]=diff*(1.f/static_cast<float>(period_/32));
        }
        const bool ramp=lane.remaining[c]>0 && !skip;
        for (unsigned f=0;f<32;++f)
            lane.cache[f*16+c]=skip ? 0.f : ramp ? (static_cast<float>(f)/32.f)*lane.delta[c]+lane.old[c] : target[c];
        if (ramp) { lane.old[c]+=lane.delta[c]; --lane.remaining[c]; }
        else if (!skip) lane.old[c]=target[c];
    }
}
void MetadataRenderer::RenderPeriod(unsigned frames) {
    std::fill(canonicalMix_.begin(),canonicalMix_.end(),0.f);
    for (auto& lane:lanes_) {
        unsigned start=0;
        while (start<frames) {
            const auto& first=lane.frames[start];
            if (!first.generation) { ++start; continue; }
            unsigned end=start+1;
            while (end<frames && lane.frames[end].generation==first.generation && lane.frames[end].bed==first.bed) ++end;
            const bool restart=lane.renderedGeneration!=first.generation;
            lane.renderedGeneration=first.generation;
            std::array<float,16> target{};
            if (first.bed>=0) target[first.bed]=1.f;
            else {
                auto xyz=lane.frames[end-1].position;
                xyz[0]=(xyz[0]+1.f)*.5f;
                xyz[1]=(1.f-xyz[1])*.5f;
                for (unsigned c=0;c<3;++c)
                    lane.smoothed[c]=restart ? xyz[c] : oldCoefficient_*lane.smoothed[c]+newCoefficient_*xyz[c];
                const auto gains=PanAdmOarPoint(canonical_,2.0*lane.smoothed[0]-1,1.0-2.0*lane.smoothed[1],lane.smoothed[2]);
                for (unsigned c=0;c<16;++c) target[c]=static_cast<float>(gains[c]);
            }
            if (restart || first.bed>=0) { lane.old=target; lane.remaining.fill(0); lane.delta.fill(0); }
            for (unsigned f=start;f<end;++f) {
                const unsigned phase=(f-start)%32;
                if (!phase) GainBlock(lane,target,f==start);
                for (unsigned c=0;c<16;++c)
                    canonicalMix_[f*16+c]+=lane.frames[f].pcm*lane.cache[phase*16+c];
            }
            start=end;
        }
    }
    std::fill(ready_.begin(),ready_.end(),0.f);
    for (unsigned f=0;f<frames;++f) for (unsigned c=0;c<channels_;++c) {
        float value=0;
        for (unsigned bus=0;bus<16;++bus) value+=canonicalMix_[f*16+bus]*static_cast<float>(projection_[bus][c]);
        if (!std::isfinite(value)) throw std::runtime_error("Metadata render overflow");
        ready_[f*channels_+c]=value;
    }
}
unsigned MetadataRenderer::Flush(std::span<float> output) {
    if (finished_ || output.size()<static_cast<std::size_t>(period_)*channels_)
        throw std::invalid_argument("Invalid metadata flush buffer or repeated flush");
    finished_=true;
    if (!position_) return 0;
    const unsigned remaining=period_-fill_;
    std::copy_n(ready_.data()+fill_*channels_,remaining*channels_,output.data());
    if (fill_) {
        RenderPeriod(fill_);
        std::copy_n(ready_.data(),fill_*channels_,output.data()+remaining*channels_);
    }
    return period_;
}
}
