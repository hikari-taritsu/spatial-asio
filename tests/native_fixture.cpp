// Opt-in hardware validation, never run by CTest. Native objects contain
// deterministic PCM but are muted through SetVolume before every submission.
#include "audio_platform.h"
#include <mmdeviceapi.h>
#include <spatialaudioclient.h>
#include <wrl/client.h>
#include <cmath>
#include <chrono>
#include <iostream>
#include <vector>
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Windows Spatial Audio HRESULT "+std::to_string(static_cast<unsigned>(hr)));}
int wmain(int argc,wchar_t** argv){try {
    if(argc!=4)throw std::runtime_error("Usage: NativeFixture ENDPOINT_ID OBJECTS SECONDS (muted)");
    const unsigned objects=std::stoul(argv[2]);const double seconds=std::stod(argv[3]);
    if(!objects||objects>128||!std::isfinite(seconds)||seconds<1||seconds>120)throw std::runtime_error("Invalid fixture dimensions");
    spatial::ComApartment apartment(COINIT_MULTITHREADED);
    ComPtr<IMMDeviceEnumerator> enumerator;Check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)));
    ComPtr<IMMDevice> endpoint;Check(enumerator->GetDevice(argv[1],&endpoint));
    ComPtr<ISpatialAudioClient> client;Check(endpoint->Activate(__uuidof(ISpatialAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())));
    UINT32 maximum{};Check(client->GetMaxDynamicObjectCount(&maximum));if(maximum<objects)throw std::runtime_error("Endpoint has insufficient dynamic object capacity");
    WAVEFORMATEX format{WAVE_FORMAT_IEEE_FLOAT,1,48000,192000,4,32,0};Check(client->IsAudioObjectFormatSupported(&format));
    spatial::WinHandle event(CreateEventW(nullptr,FALSE,FALSE,nullptr));
    SpatialAudioObjectRenderStreamActivationParams parameters{};parameters.ObjectFormat=&format;
    parameters.StaticObjectTypeMask=AudioObjectType_None;parameters.MinDynamicObjectCount=objects;parameters.MaxDynamicObjectCount=objects;
    parameters.Category=AudioCategory_GameEffects;parameters.EventHandle=event.Get();
    PROPVARIANT activation{};activation.vt=VT_BLOB;activation.blob.cbSize=sizeof(parameters);activation.blob.pBlobData=reinterpret_cast<BYTE*>(&parameters);
    ComPtr<ISpatialAudioObjectRenderStream> stream;Check(client->ActivateSpatialAudioStream(&activation,__uuidof(ISpatialAudioObjectRenderStream),reinterpret_cast<void**>(stream.GetAddressOf())));
    std::vector<ComPtr<ISpatialAudioObject>> lanes(objects);unsigned blocks=0;std::uint64_t submitted=0,restarts=0;
    Check(stream->Start());const auto began=std::chrono::steady_clock::now();
    while(std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<seconds) {
        if(WaitForSingleObject(event.Get(),2000)!=WAIT_OBJECT_0)throw std::runtime_error("Spatial event timeout");
        UINT32 available{},frames{};Check(stream->BeginUpdatingAudioObjects(&available,&frames));
        for(unsigned object=0;object<objects;++object) {
            if(!lanes[object]){Check(stream->ActivateSpatialAudioObject(AudioObjectType_Dynamic,&lanes[object]));++restarts;}
            auto& lane=lanes[object];
            Check(lane->SetPosition(std::sin((blocks+object)*.013f),(std::sin((blocks+object)*.023f)+1)*.5f,-std::cos((blocks+object)*.017f)));
            Check(lane->SetVolume(0));BYTE* buffer{};UINT32 bytes{};Check(lane->GetBuffer(&buffer,&bytes));
            if(bytes!=frames*sizeof(float))throw std::runtime_error("Unexpected spatial buffer format");
            auto* samples=reinterpret_cast<float*>(buffer);
            for(unsigned f=0;f<frames;++f)samples[f]=((f+object+blocks)%113-56.0f)*.0001f;
            if(blocks%97==96 && object%7==0)Check(lane->SetEndOfStream(frames));
        }
        Check(stream->EndUpdatingAudioObjects());
        if(blocks%97==96)for(unsigned object=0;object<objects;object+=7)lanes[object].Reset();
        ++blocks;submitted+=frames;
    }
    Check(stream->Stop());
    std::cout<<"{\"objects\":"<<objects<<",\"blocks\":"<<blocks<<",\"frames\":"<<submitted<<",\"activations\":"<<restarts<<",\"muted\":true}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
