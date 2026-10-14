#include "metadata_renderer.h"
#include "native_spatial.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace spatial;
void Check(bool b,const char* what){if(!b)throw std::runtime_error(what);}
SpeakerLayout Layout(const wchar_t* names) {
    SpeakerLayout result;std::wistringstream text(names);std::wstring name;
    const auto full=Canonical916Layout();
    while(text>>name)result.speakers.push_back(full.speakers[*full.FindSpeaker(name)]);
    return result;
}
void CheckLayout(const SpeakerLayout& layout) {
    const auto count=layout.speakers.size();
    for(int x=-10;x<=10;++x)for(int y=-10;y<=10;++y)for(int z=0;z<=10;++z) {
        const auto g=PanAdmOarPoint(layout,x/10.,y/10.,z/10.);
        const auto mirror=PanAdmOarPoint(layout,-x/10.,y/10.,z/10.);
        double power=0;
        for(std::size_t c=0;c<count;++c) {
            Check(std::isfinite(g[c])&&g[c]>=0,"Invalid layout gain");power+=g[c]*g[c];
            auto name=layout.speakers[c].name;
            if(name.ends_with(L"L"))name.back()=L'R';else if(name.ends_with(L"R"))name.back()=L'L';
            if(auto opposite=layout.FindSpeaker(name))Check(std::abs(g[c]-mirror[*opposite])<.014f,"Left/right asymmetry exceeds coordinate quantization");
        }
        Check(std::abs(power-1)<1e-5,"Selected-layout power changed with position");
        if(auto lfe=layout.FindSpeaker(L"LFE"))Check(g[*lfe]==0,"Dynamic object entered LFE");
    }
    for(int axis=0;axis<3;++axis) {
        std::vector<double> previous;
        for(int n=0;n<=1000;++n) {
            double xyz[]{.17f,-.23f,.37f};xyz[axis]=axis==2?n/1000.:n/500.-1;
            auto g=PanAdmOarPoint(layout,xyz[0],xyz[1],xyz[2]);
            if(!previous.empty())for(std::size_t c=0;c<count;++c)Check(std::abs(g[c]-previous[c])<.055f,"Discontinuous layout trajectory");
            previous=g;
        }
    }
    auto reversed=layout;std::reverse(reversed.speakers.begin(),reversed.speakers.end());
    const auto forward=PanAdmOarPoint(layout,.37f,.2f,.82f),back=PanAdmOarPoint(reversed,.37f,.2f,.82f);
    for(std::size_t c=0;c<count;++c)Check(forward[c]==back[count-1-c],"Physical ordering changed semantic panning");
    MetadataRenderer whole(layout,48000,3),chunked(layout,48000,3);
    constexpr unsigned frames=2401;
    std::vector<float> input(frames*3,.125f),a(frames*count),b(a.size());
    const std::vector<MetadataEvent> events{
        {0,0,MetadataBegin|MetadataPosition|MetadataGain,-1,-.5f,.25f,.8f,.7f,0},
        {123,2,MetadataBegin|MetadataPosition|MetadataGain,-1,.6f,-.7f,.3f,1,0},
        {411,0,MetadataGain,-1,0,0,0,.1f,733},{512,2,MetadataEnd},
        {913,2,MetadataBegin|MetadataPosition|MetadataGain,-1,-.2f,.8f,1,.5f,0},
        {1300,0,MetadataPosition,-1,.3f,-.5f,.1f},{2200,2,MetadataEnd}};
    whole.Process(input,frames,events,a);
    for(unsigned from=0;from<frames;) {
        const auto n=std::min(67U,frames-from);std::vector<MetadataEvent> e;
        for(auto v:events)if(v.offset>=from&&v.offset<from+n){v.offset-=from;e.push_back(v);}
        chunked.Process(std::span(input).subspan(from*3,n*3),n,e,std::span(b).subspan(from*count,n*count));from+=n;
    }
    Check(a==b,"Layout lifecycle/gain depends on caller block size");
    std::vector<float> tail(512*count),other(tail.size());whole.Flush(tail);chunked.Flush(other);Check(tail==other,"Layout tail mismatch");
    whole.Reset();whole.Process(input,frames,events,b);Check(a==b,"Layout reset changed output");
    // All input bed identities use either direct semantic output or the target
    // panner at their position, including absent height/rear/center channels.
    for(int bed=0;bed<16;++bed) {
        MetadataRenderer renderer(layout,48000,1);std::vector<float> in(1024,.25f),out(1024*count);
        const MetadataEvent event{0,0,MetadataBegin|MetadataBed,bed};renderer.Process(in,1024,{&event,1},out);
        double energy=0;for(std::size_t c=0;c<count;++c)energy+=std::pow(out[512*count+c]/.25f,2);
        Check(std::abs(energy-(bed==3&&!layout.FindSpeaker(L"LFE")?0:1))<1e-5,"Bed position remap loses or amplifies power");
    }
    // This must be an actual direct render, not merely a direct panner utility.
    for(const auto xyz: {std::array<float,3>{-.5f,.25f,1},std::array<float,3>{.31f,-.41f,.7f}}) {
        MetadataRenderer renderer(layout,48000,1);std::vector<float> in(1024,.25f),out(1024*count);
        const MetadataEvent e{0,0,MetadataBegin|MetadataPosition,-1,xyz[0],xyz[1],xyz[2]};
        renderer.Process(in,1024,{&e,1},out);const auto target=PanAdmOarPoint(layout,xyz[0],xyz[1],xyz[2]);
        for(std::size_t c=0;c<count;++c)Check(std::abs(out[512*count+c]-.25f*target[c])<1e-6,"Renderer bypasses selected-layout panner");
    }
}
int main(){try {
    for(const auto names:{L"FL FR FC LFE BL BR SL SR FWL FWR TFL TFR TML TMR TBL TBR",L"FL FR FC LFE BL BR SL SR TFL TFR TBL TBR",
        L"FL FR FC LFE BL BR SL SR TML TMR",L"FL FR FC LFE SL SR TFL TFR TBL TBR",L"FL FR FC LFE SL SR TML TMR",L"FL FR FC LFE SL SR",L"FL FR"})CheckLayout(Layout(names));
    // Ring wrap and mixing of simultaneous streams preserve PCM exactly.
    NativeSpatialTimeline timeline(Canonical916Layout());
    for(unsigned block=0;block<600;++block) {
        NativePacket p;p.header.objects=1;p.header.frames=512;p.header.bytes=2080;p.header.sequence=block;
        p.header.captureNs=1'000'000'000ULL+block*512ULL*1'000'000'000/48000;p.planar.assign(512,.125f);p.properties={{1,512,2,2,0,0,0,1}};
        for(unsigned stream=1;stream<=2;++stream){p.header.stream=stream;timeline.Add(p);}
        auto audio=timeline.Take((block+1)*512ULL);
        for(unsigned f=0;f<512;++f)for(unsigned c=0;c<16;++c)Check(audio[f*16+c]==(block&&c==0?.25f:0.f),"Timeline wrap/multi-stream corruption");
    }
    Check(timeline.Stats().lateFrames==0,"Unexpected late frames in ring test");
    std::cout<<"Seven direct layouts: 33957 positions, power, symmetry, trajectories, semantic order, bed identities, lifecycle, gain, reset, tail, direct-render path; multi-stream ring wrap passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
