#include "native_spatial.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
using namespace spatial;
int main(int argc,char** argv) {
    std::ofstream output;
    if(argc==2)output.open(argv[1],std::ios::binary);
    for(unsigned objects:{4U,32U,160U}) {
        NativeSpatialTimeline timeline(Canonical916Layout());
        std::mt19937 random(7125);std::uniform_real_distribution<float> signal(-.002f,.002f);
        std::vector<double> costs;double total=0;constexpr unsigned frames=128,blocks=1200;
        for(unsigned block=0;block<blocks;++block) {
            NativePacket packet{};auto& h=packet.header;
            h.magic=0x3150534e;h.sampleRate=48000;h.frames=frames;
            h.objects=objects-(block%79==0?1:0);h.bytes=h.objects*(32+frames*4);
            h.stream=1;h.sequence=block;h.captureNs=1'000'000'000ULL+static_cast<std::uint64_t>(block)*frames*1'000'000'000/48000;
            packet.properties.resize(h.objects);packet.planar.resize(h.objects*frames);
            for(unsigned o=0;o<h.objects;++o) {
                auto& p=packet.properties[o];p.candidateId=o+1000*(block/149);p.type=1;p.flags=6;p.frames=frames;
                p.x=std::sin((block+o)*.017f);p.y=(std::sin((block+o)*.029f)+1)*.5f;
                p.z=std::cos((block+o)*.021f);p.gain=(block%31)/31.f;
            }
            for(auto& sample:packet.planar)sample=signal(random);
            auto began=std::chrono::steady_clock::now();
            timeline.Add(packet);
            auto audio=timeline.Take(block>12?(block-12)*frames:0);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
            total+=ms;costs.push_back(ms);
            if(output)output.write(reinterpret_cast<const char*>(audio.data()),audio.size()*4);
        }
        timeline.Complete();const auto tail=timeline.Take(timeline.EndFrame());
        if(output)output.write(reinterpret_cast<const char*>(tail.data()),tail.size()*4);
        std::sort(costs.begin(),costs.end());
        std::cout<<objects<<" objects: mean_ms="<<total/blocks<<" p95_ms="<<costs[blocks*95/100]<<" max_ms="<<costs.back()<<" cpu_percent="<<total/(blocks*frames/48.)*100<<'\n';
    }
}
