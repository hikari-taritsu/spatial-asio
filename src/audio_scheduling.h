#pragma once
#include <windows.h>
#include <avrt.h>
#include <mmsystem.h>
namespace spatial {
// These requests are scoped to the running bridge, not persisted system tweaks.
class AudioScheduling {
    HANDLE task_{};bool timer_{};
public:
    explicit AudioScheduling(bool timer=true) {
        timer_=timer && timeBeginPeriod(1)==TIMERR_NOERROR;
        DWORD index{};task_=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
        if(task_)AvSetMmThreadPriority(task_,AVRT_PRIORITY_HIGH);
    }
    ~AudioScheduling(){if(task_)AvRevertMmThreadCharacteristics(task_);if(timer_)timeEndPeriod(1);}
    AudioScheduling(const AudioScheduling&)=delete;
    AudioScheduling& operator=(const AudioScheduling&)=delete;
    bool Enabled() const{return task_!=nullptr;}
};
}
