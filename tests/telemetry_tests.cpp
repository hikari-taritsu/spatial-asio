#include "telemetry_sink.h"
#include "audio_platform.h"
#include <chrono>
#include <iostream>
int main(){try{
    HANDLE read{},write{};if(!CreatePipe(&read,&write,nullptr,4096))return 2;
    spatial::WinHandle input(read),output(write);
    const auto began=std::chrono::steady_clock::now();
    {
        spatial::TelemetrySink sink(write);
        sink.Publish(std::string(65536,'x'));Sleep(20); // Reader intentionally does not drain.
        for(unsigned i=0;i<10000;++i)sink.Publish("{\"event\":\"status\"}\n");
    }
    if(std::chrono::steady_clock::now()-began>std::chrono::seconds(2))return 3;
    std::cout<<"Blocked telemetry pipe does not block producer or shutdown\n";return 0;
}catch(...){return 1;}}
