#pragma once
#include <windows.h>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
namespace spatial {
// A slow/minimized UI must never backpressure the audio worker. Only the newest
// unsent status is retained; this queue contains diagnostics, never audio.
class TelemetrySink {
    HANDLE output_;std::mutex mutex_;std::condition_variable changed_;
    std::string latest_;bool stopping_{};std::thread writer_;
    void Write() {
        while(true) {
            std::string message;
            {std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return stopping_||!latest_.empty();});
             if(latest_.empty()&&stopping_)return;message=std::move(latest_);latest_.clear();}
            DWORD written{};
            for(std::size_t offset=0;offset<message.size();offset+=written)
                if(!WriteFile(output_,message.data()+offset,static_cast<DWORD>(message.size()-offset),&written,nullptr)||!written)return;
        }
    }
public:
    explicit TelemetrySink(HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE)):output_(output),writer_([this]{Write();}){}
    void Publish(std::string message) {std::lock_guard lock(mutex_);latest_=std::move(message);changed_.notify_one();}
    ~TelemetrySink() {
        {std::lock_guard lock(mutex_);stopping_=true;changed_.notify_one();}
        // Finish the last status for a responsive reader; cancel blocked pipe
        // writes on exit, without terminating a thread or the parent process.
        while(WaitForSingleObject(writer_.native_handle(),300)==WAIT_TIMEOUT)CancelSynchronousIo(writer_.native_handle());
        writer_.join();
    }
    TelemetrySink(const TelemetrySink&)=delete;
    TelemetrySink& operator=(const TelemetrySink&)=delete;
};
}
