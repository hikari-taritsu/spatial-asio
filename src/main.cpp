#include "audio_platform.h"
#include "asio_backend.h"
#include "native_spatial.h"
#include "bridge_stop.h"
#include "json_text.h"
#include "audio_scheduling.h"
#include "telemetry_sink.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <numeric>
#include <sstream>
namespace spatial {
namespace {
#include "capture_process.inc"
void Run(const std::filesystem::path& profile,double gain,DWORD prebuffer,double seconds) {
    if(!std::isfinite(gain) || gain<0 || gain>1 || prebuffer<8 || prebuffer>500 || !std::isfinite(seconds) || seconds<0 || seconds>28800) throw std::runtime_error("Invalid gain, buffer or duration");
    WinHandle single(CreateMutexW(nullptr,FALSE,L"Local\\SpatialASIO.Capture"));
    if(!single.IsValid() || GetLastError()==ERROR_ALREADY_EXISTS) throw std::runtime_error("Spatial ASIO capture is already running");
    const auto layout=LoadSpeakerLayout(profile);
    const auto channels=layout.speakers.size();
    AudioScheduling scheduling;
    const auto logs=std::filesystem::absolute(profile).parent_path().parent_path()/L"Logs";
    std::filesystem::create_directories(logs);
    NativeSpatialTimeline timeline(layout);
    InterleavedPcmQueue queue(channels);
    // Driver initialization may block. Finish it before capture timestamps begin
    // so initialization cannot silently become permanent audio queue latency.
    auto renderer=std::make_unique<AsioRenderer>(layout,gain,RendererLatencyMode::Balanced);
    CaptureProcess capture(AdapterDirectory(),logs/(L"capture-"+std::to_wstring(GetCurrentProcessId())+L".json"),seconds);
    std::vector<double> trims;for(const auto& speaker:layout.speakers)trims.push_back(std::pow(10.,speaker.trimDb/20.));
    double renderTotalMs=0,renderMaxMs=0,captureMaxMs=0;std::uint64_t measuredPackets=0;
    LARGE_INTEGER qpcFrequency{};QueryPerformanceFrequency(&qpcFrequency);
    std::vector<double> peaks(channels),squares(channels); std::uint64_t meterFrames=0;
    const auto began=std::chrono::steady_clock::now();
    auto first=began,lastPacket=began,lastStatus=began;
    bool received=false,stopSent=false; auto stopTime=began;
    std::string control; const HANDLE input=GetStdHandle(STD_INPUT_HANDLE);
    const bool pipeInput=GetFileType(input)==FILE_TYPE_PIPE;
    TelemetrySink telemetry;
    telemetry.Publish("{\"event\":\"starting\",\"channels\":"+std::to_string(channels)+",\"renderer\":\"oar-point\",\"lookaheadFrames\":512}\n");
    auto emit=[&](std::uint64_t until) {
        if(until<=timeline.Cursor())return;
        auto samples=timeline.Take(until);
        for(std::size_t i=0;i<samples.size();++i) {
            const auto c=i%channels;
            const double v=samples[i]*gain*trims[c];
            peaks[c]=std::max(peaks[c],std::abs(v)); squares[c]+=v*v;
        }
        meterFrames+=samples.size()/channels;
        queue.AppendFloat(std::move(samples));
    };
    try {
        while(true) {
            auto now=std::chrono::steady_clock::now();
            if(pipeInput) {
                DWORD available{};
                if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr))SetEvent(BridgeStopEvent());
                if(available) {
                    char chunk[1024];DWORD read{};
                    if(ReadFile(input,chunk,std::min<DWORD>(available,sizeof(chunk)),&read,nullptr))control.append(chunk,read);
                    if(control.size()>4096)throw std::runtime_error("Control message exceeds limit");
                    for(auto end=control.find('\n');end!=std::string::npos;end=control.find('\n')) {
                        auto message=control.substr(0,end);control.erase(0,end+1);
                        if(message=="stop" || message=="stop\r")SetEvent(BridgeStopEvent());
                        else if(message.starts_with("gain ")) {
                            gain=std::stod(message.substr(5));
                            if(!std::isfinite(gain)||gain<0||gain>1)throw std::runtime_error("Invalid live gain");
                            if(renderer)renderer->SetGain(gain);
                        }
                    }
                }
            }
            if(seconds && now-began>=std::chrono::duration<double>(seconds)) SetEvent(BridgeStopEvent());
            if(BridgeStopRequested() && !stopSent) { stopSent=true;stopTime=now; }
            if(stopSent && now-stopTime>std::chrono::seconds(12)) throw std::runtime_error("Capture cleanup timed out; inspect capture log");
            NativePacket packet;
            const auto batchStart=std::chrono::steady_clock::now();
            const double batchBudgetMs=std::max(.5,renderer->PeriodMilliseconds()*.5);
            for(int i=0;i<8 && capture.Pop(packet);++i) {
                if(!received) { received=true;first=now; }
                const auto renderStart=std::chrono::steady_clock::now();
                LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);
                captureMaxMs=std::max(captureMaxMs,static_cast<double>(qpc.QuadPart)*1000/qpcFrequency.QuadPart-packet.header.captureNs/1e6);
                lastPacket=renderStart;timeline.Add(packet);
                const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-renderStart).count();
                renderTotalMs+=ms;renderMaxMs=std::max(renderMaxMs,ms);++measuredPackets;
                if(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-batchStart).count()>=batchBudgetMs)break;
            }
            now=std::chrono::steady_clock::now();
            const bool done=capture.Finished();
            if(received && !done) {
                const auto frame=static_cast<std::uint64_t>(std::chrono::duration<double>(now-first).count()*48000);
                if(frame>prebuffer*48ULL) emit(std::max(timeline.Cursor(),frame-prebuffer*48ULL));
            }
            if(done && received) { timeline.Complete();emit(std::max(timeline.Cursor(),timeline.EndFrame())); }
            if(renderer) {
                if(!renderer->IsStarted() && received && (queue.EndFrame()>=renderer->MaximumBufferFrames() || done)) { renderer->Prime(queue);renderer->Start(); }
                if(renderer->IsStarted()) { renderer->Service(queue,!done,1,capture.available.Get());renderer->DiscardConsumed(queue); }
                if(renderer->MinimumFramesAvailable(queue)>48000)throw std::runtime_error("ASIO output queue stalled");
            }
            if(now-lastStatus>=std::chrono::milliseconds(250)) {
                const auto stats=timeline.Stats();
                const auto out=renderer ? renderer->Stats()[0] : EndpointRenderStats{};
                const double age=received ? std::chrono::duration<double>(now-lastPacket).count() : -1;
                std::ostringstream status;
                status<<"{\"event\":\"status\",\"receiving\":"<<(age>=0 && age<.5 ? "true":"false")<<",\"asioRunning\":"<<(renderer && renderer->IsStarted() ? "true":"false")
                    <<",\"packets\":"<<stats.packets<<",\"objects\":"<<(stats.creates-stats.retires)<<",\"gaps\":"<<stats.gaps<<",\"unknownState\":"<<stats.unknownStateBlocks
                    <<",\"lateFrames\":"<<stats.lateFrames<<",\"underruns\":"<<out.starvationFrames<<",\"bufferFrames\":"<<out.bufferFrames
                    <<",\"captureBufferMs\":"<<prebuffer<<",\"lookaheadMs\":"<<512/48.0<<",\"asioPeriodMs\":"<<out.bufferMilliseconds
                    <<",\"driverLatencyMs\":"<<out.streamLatencyMilliseconds-2*out.bufferMilliseconds
                    <<",\"queuedMs\":"<<renderer->BufferedFrames(queue)/48.0
                    <<",\"renderMeanMs\":"<<(measuredPackets?renderTotalMs/measuredPackets:0)<<",\"renderMaxMs\":"<<renderMaxMs
                    <<",\"captureMaxMs\":"<<captureMaxMs<<",\"mmcss\":"<<(scheduling.Enabled()?"true":"false")<<",\"peak\":[";
                for(std::size_t c=0;c<channels;++c) {if(c)status<<',';status<<peaks[c];}
                status<<"],\"rms\":[";
                for(std::size_t c=0;c<channels;++c) {if(c)status<<',';status<<(meterFrames ? std::sqrt(squares[c]/meterFrames):0);}
                status<<"]}\n";telemetry.Publish(status.str());
                std::fill(peaks.begin(),peaks.end(),0);std::fill(squares.begin(),squares.end(),0);meterFrames=0;lastStatus=now;
            }
            if(done && (!renderer || renderer->IsDrained(queue)))break;
            if(!received && now-began>std::chrono::seconds(120))throw std::runtime_error("No spatial objects received: enable Windows Sonic on the game's output endpoint, then restart the game");
            if(!renderer->IsStarted())WaitForSingleObject(capture.available.Get(),1);
        }
        if(renderer)renderer->Stop();
        timeline.Finish();
        if(capture.ExitCode()!=0)throw std::runtime_error("Native capture failed; see capture log");
        telemetry.Publish("{\"event\":\"stopped\"}\n");
    } catch(...) { SetEvent(BridgeStopEvent());if(renderer)renderer->Stop();throw; }
}
}
}
int wmain(int argc,wchar_t** argv) {
    using namespace spatial;
    try {
        ComApartment com(COINIT_APARTMENTTHREADED);
        if(argc<2)throw std::runtime_error("Commands: drivers | probe ASIO_ID | panel ASIO_ID | validate PROFILE | run PROFILE [gain 0..1] [prebuffer_ms 8..500] [seconds] | stop PID");
        const std::wstring command=argv[1];
        if(command==L"drivers" && argc==2)PrintAsioDriversJson();
        else if(command==L"probe" && argc==3)ProbeAsioDriverJson(argv[2]);
        else if(command==L"panel" && argc==3)ShowAsioControlPanel(argv[2]);
        else if(command==L"validate" && argc==3) {const auto p=LoadSpeakerLayout(argv[2]);std::cout<<"{\"valid\":true,\"channels\":"<<p.speakers.size()<<"}\n";}
        else if(command==L"stop" && argc==3) {
            const unsigned long pid=std::stoul(argv[2]);
            WinHandle event(OpenEventW(EVENT_MODIFY_STATE,FALSE,(L"Local\\SpatialASIO.Stop."+std::to_wstring(pid)).c_str()));
            if(!event.IsValid() || !SetEvent(event.Get()))throw std::runtime_error("Capture owner not found");
        } else if(command==L"run" && argc>=3 && argc<=6) {
            BridgeStopEvent();SetConsoleCtrlHandler(BridgeConsoleHandler,TRUE);
            Run(argv[2],argc>3?std::stod(argv[3]):.251188643,argc>4?std::stoul(argv[4]):32,argc>5?std::stod(argv[5]):0);
        } else throw std::runtime_error("Unsupported command; this product accepts only native object capture and ASIO output");
        return 0;
    }catch(const std::exception& e) { std::cerr<<e.what()<<'\n'<<std::flush;return 1; }
}
