#pragma once
#include "audio_platform.h"
#include <string>
namespace spatial {
inline HANDLE BridgeStopEvent() {
    static WinHandle event(CreateEventW(nullptr, TRUE, FALSE,
        (L"Local\\SpatialASIO.Stop." + std::to_wstring(GetCurrentProcessId())).c_str()));
    return event.Get();
}
inline bool BridgeStopRequested() { return WaitForSingleObject(BridgeStopEvent(), 0) == WAIT_OBJECT_0; }
inline BOOL WINAPI BridgeConsoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        SetEvent(BridgeStopEvent()); return TRUE;
    }
    return FALSE;
}
}
