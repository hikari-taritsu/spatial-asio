#include "audio_platform.h"
#include "asio_backend.h"
#include "iasiodrv.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void Expect(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
long preferred=128, failCreate=0;
int opens=0, releases=0, disposes=0, stops=0;
class FakeDriver : public IASIO {
public:
    ASIOCallbacks callbacks{};
    std::vector<ASIOBufferInfo> buffers;
    std::vector<std::vector<float>> memory;
    bool running=false;
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n=--refs; if (!n) { ++releases; delete this; } return n; }
    ASIOBool init(void*) override { return ASIOTrue; }
    void getDriverName(char* s) override { strcpy_s(s,32,"Test"); }
    long getDriverVersion() override { return 1; }
    void getErrorMessage(char* s) override { strcpy_s(s,124,"Test error"); }
    ASIOError start() override { running=true; return ASE_OK; }
    ASIOError stop() override { running=false; ++stops; return ASE_OK; }
    ASIOError getChannels(long* i,long* o) override { *i=0;*o=32;return ASE_OK; }
    ASIOError getLatencies(long* i,long* o) override { *i=*o=0;return ASE_OK; }
    ASIOError getBufferSize(long* a,long* b,long* p,long* g) override { *a=64;*b=256;*p=preferred;*g=64;return ASE_OK; }
    ASIOError canSampleRate(double r) override { return r==48000?ASE_OK:ASE_NoClock; }
    ASIOError getSampleRate(double* r) override { *r=48000;return ASE_OK; }
    ASIOError setSampleRate(double r) override { return canSampleRate(r); }
    ASIOError getClockSources(ASIOClockSource*,long* n) override { *n=0;return ASE_OK; }
    ASIOError setClockSource(long) override { return ASE_NotPresent; }
    ASIOError getSamplePosition(ASIOSamples*,ASIOTimeStamp*) override { return ASE_NotPresent; }
    ASIOError getChannelInfo(ASIOChannelInfo* i) override { i->type=ASIOSTFloat32LSB;return ASE_OK; }
    ASIOError createBuffers(ASIOBufferInfo* out,long n,long size,ASIOCallbacks* cb) override {
        if (failCreate) return ASE_InvalidMode;
        callbacks=*cb; memory.assign(n*2,std::vector<float>(size,123));
        for(long c=0;c<n;++c) for(int half=0;half<2;++half) out[c].buffers[half]=memory[c*2+half].data();
        buffers.assign(out,out+n); return ASE_OK;
    }
    ASIOError disposeBuffers() override { Expect(!running,"dispose while started"); ++disposes; return ASE_OK; }
    ASIOError controlPanel() override { return ASE_OK; }
    ASIOError future(long,void*) override { return ASE_NotPresent; }
    ASIOError outputReady() override { return ASE_OK; }
    void Tick(int index=0) { Expect(running,"callback without start"); callbacks.bufferSwitch(index,ASIOTrue); }
};
FakeDriver* current=nullptr;
}
namespace spatial {
IASIO* MakeTestAsioDriver() { ++opens; return current=new FakeDriver; }
}
int main() {
    using namespace spatial;
    try {
        InterleavedPcmQueue precision(2);
        precision.AppendFloat({1e-8F, -1e-8F});
        Expect(precision.Sample(0,0)==1e-8F && precision.Sample(0,1)==-1e-8F,"float PCM quantized before ASIO");
        SpeakerLayout layout;
        for(int i=0;i<16;++i) { SpeakerDefinition speaker; speaker.name=L"S"+std::to_wstring(i); layout.speakers.push_back(speaker); }
        OutputRouteDefinition route; route.name=L"ASIO";route.endpointFilter=L"asio:test";
        for(int i=0;i<16;++i) { route.speakerIndices.push_back(i);route.physicalChannels.push_back(31-i); }
        layout.outputs.push_back(route);
        {
            AsioRenderer renderer(layout,1,RendererLatencyMode::Balanced);
            for(int c=0;c<16;++c) {
                Expect(current->buffers[c].channelNum==31-c,"physical mapping lost");
                for(float x:current->memory[c*2]) Expect(x==0,"initial buffer not silent");
            }
            InterleavedPcmQueue queue(16);
            std::vector<std::int16_t> samples(512*16);
            for(int f=0;f<512;++f) for(int c=0;c<16;++c) samples[f*16+c]=static_cast<std::int16_t>(1024*(c+1));
            queue.Append(std::move(samples)); renderer.Prime(queue); renderer.Start();
            renderer.Service(queue,true,0); current->Tick();
            for(int c=0;c<16;++c) Expect(std::abs(current->memory[c*2][63]-(c+1)/32.f)<1e-6,"channel sample mismatch");
            const auto beforePause=renderer.Stats()[0].clockSeconds;
            renderer.Pause();
            Expect(!current->running && !renderer.IsStarted(),"pause did not stop callbacks");
            Expect(renderer.Stats()[0].clockSeconds==beforePause,"pause advanced the hardware clock");
            renderer.Start();
            // Producer pause drains two periods, then emits zeros and counts exact missing frames.
            current->Tick(1); current->Tick();
            Expect(renderer.Stats()[0].starvationFrames==128,"underrun count mismatch");
            for(auto& channel:current->memory) for(float value:channel) (void)value;
            for(int c=0;c<16;++c) for(float x:current->memory[c*2]) Expect(x==0,"underrun was not silent");
            // Buffer-size/reset requests rebuild on Service, preserving source at callback consumption.
            preferred=64;
            Expect(current->callbacks.asioMessage(kAsioBufferSizeChange,64,nullptr,nullptr)==1,"reset not acknowledged");
            renderer.Service(queue,true,0);
            Expect(opens==2 && disposes==1 && renderer.Stats()[0].bufferFrames==64,"reset lifecycle failed");
            current->Tick(); current->Tick(1);
            renderer.Stop();
        }
        Expect(opens==releases && disposes==2 && stops==3,"driver leaked after stop/pause");
        failCreate=1;
        bool failed=false; try { AsioRenderer bad(layout,1,RendererLatencyMode::Balanced); } catch(...) { failed=true; }
        Expect(failed && opens==releases,"createBuffers failure leaked driver");
        failCreate=0;
        { AsioRenderer next(layout,1,RendererLatencyMode::Balanced); }
        Expect(opens==releases,"driver could not reopen after failure");
        std::cout<<"ASIO 16-channel callback mapping, silence, underrun, reset, stop and failure cleanup passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
