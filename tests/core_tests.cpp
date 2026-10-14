#include "metadata_renderer.h"
#include "native_spatial.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace spatial;
void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
int main(){try{
    const auto layout=Canonical916Layout();
    auto reduced=layout;reduced.speakers.erase(reduced.speakers.begin()+14,reduced.speakers.end());reduced.speakers.erase(reduced.speakers.begin()+8,reduced.speakers.begin()+12);
    const auto folding=BuildCanonical916Mix(reduced);
    require(folding[10][*reduced.FindSpeaker(L"TML")]==1&&folding[14][*reduced.FindSpeaker(L"TML")]==1,"7.1.2 must preserve front/rear height energy in TML");
    for(int x=-10;x<=10;++x)for(int y=-10;y<=10;++y)for(int z=0;z<=10;++z){
        auto g=PanAdmOarPoint(layout,x/10.,y/10.,z/10.);double power=0;
        for(double v:g){require(std::isfinite(v)&&v>=0,"Invalid point gain");power+=v*v;}
        require(std::abs(power-1)<1e-5,"Point power normalization");require(g[3]==0,"Object sent to LFE");
    }
    MetadataRenderer whole(layout,48000,1),chunked(layout,48000,1);
    std::vector<float> input(2048,.25f),a(2048*16),b(2048*16);
    std::vector<MetadataEvent> events{{0,0,MetadataBegin|MetadataPosition|MetadataGain,-1,-1,1,0,.5f,0},{333,0,MetadataGain,-1,0,0,0,0,257},{800,0,MetadataEnd},{910,0,MetadataBegin|MetadataPosition|MetadataGain,-1,1,-1,1,1,0}};
    whole.Process(input,2048,events,a);
    for(unsigned from=0;from<2048;){unsigned n=std::min(73U,2048-from);std::vector<MetadataEvent> e;
        for(auto event:events)if(event.offset>=from&&event.offset<from+n){event.offset-=from;e.push_back(event);}
        chunked.Process(std::span(input).subspan(from,n),n,e,std::span(b).subspan(from*16,n*16));from+=n;
    }
    require(a==b,"Metadata chunk partition altered rendering");
    for(int i=0;i<512*16;++i)require(a[i]==0,"Lookahead prefix must be silent");
    std::vector<float> ta(8192),tb(8192);require(whole.Flush(ta)==512&&chunked.Flush(tb)==512&&ta==tb,"Final lookahead mismatch");
    bool duplicateFlushRejected=false;try{whole.Flush(ta);}catch(...){duplicateFlushRejected=true;}require(duplicateFlushRejected,"Repeated flush accepted");
    whole.Reset();whole.Process(input,2048,events,b);require(a==b,"Reset retained state");
    NativePacket p; p.header.objects=1;p.header.frames=512;p.header.bytes=32+2048;p.header.stream=1;p.header.captureNs=1000000000;p.header.sequence=1;
    p.planar.assign(512,.1f);p.properties={{12,512,1,6,-1,0,-1,1}};
    NativeSpatialScene scene(layout);scene.Render(p);++p.header.sequence;auto active=scene.Render(p);
    require(active[0]>.09,"Dynamic object first block initialization failed");
    p.header.sequence+=2;p.properties[0].flags=0;auto gap=scene.Render(p);
    for(auto v:gap)require(v==0,"Gap reused unknown metadata");
    require(scene.Stats().gaps==1&&scene.Stats().unknownStateBlocks==1,"Gap stats");
    auto bad=p.header;bad.frames=4097;bool rejected=false;try{ValidateNativeHeader(bad);}catch(...){rejected=true;}require(rejected,"Oversized packet accepted");
    std::cout<<"Point power (4851 positions), LFE isolation, gain ramps, lifecycle, chunk invariance, reset, lookahead, native gap and packet bounds passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
