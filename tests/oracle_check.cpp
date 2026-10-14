// Optional local migration check. No reference binary is shipped or linked.
#include "metadata_renderer.h"
#include <windows.h>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
int wmain(int argc,wchar_t**argv){
    if(argc!=2)return 2;
    auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module)return 3;
    auto create=reinterpret_cast<void*(*)(const wchar_t*,unsigned,unsigned)>(GetProcAddress(module,"SpatialMetadataCreate"));
    auto destroy=reinterpret_cast<void(*)(void*)>(GetProcAddress(module,"SpatialMetadataDestroy"));
    auto process=reinterpret_cast<int(*)(void*,const float*,unsigned,const spatial::MetadataEvent*,unsigned,float*)>(GetProcAddress(module,"SpatialMetadataProcess"));
    if(!create||!destroy||!process)return 4;
    constexpr unsigned objects=4,frames=127;auto* oracle=create(nullptr,48000,objects);if(!oracle)return 5;
    spatial::MetadataRenderer current(spatial::Canonical916Layout(),48000,objects);
    std::mt19937 rng(485116);std::uniform_real_distribution<float> value(-1,1);
    std::vector<float> input(frames*objects),actual(frames*16),expected(frames*16);
    float maximum=0;unsigned mismatches=0;
    for(unsigned block=0;block<800;++block){
        for(auto& x:input)x=value(rng)*.125f;
        std::vector<spatial::MetadataEvent> events;
        for(unsigned obj=0;obj<objects;++obj){unsigned flags=spatial::MetadataPosition|spatial::MetadataGain;if(block%47==0)flags|=spatial::MetadataBegin;if(block%47==43)flags|=spatial::MetadataEnd;
            events.push_back({0,obj,flags,-1,value(rng),value(rng),(value(rng)+1)/2,(value(rng)+1)/2,block%11?511U:0U});}
        if(process(oracle,input.data(),frames,events.data(),static_cast<unsigned>(events.size()),expected.data()))return 6;
        current.Process(input,frames,events,actual);
        for(std::size_t i=0;i<actual.size();++i){maximum=std::max(maximum,std::abs(actual[i]-expected[i]));mismatches+=actual[i]!=expected[i];}
    }
    destroy(oracle);FreeLibrary(module);std::cout<<"800 blocks / 4 objects, moving positions, gain ramps, restarts: max error="<<maximum<<", differing samples="<<mismatches<<"\n";return mismatches?1:0;
}
