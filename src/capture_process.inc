std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\""; std::size_t slashes = 0;
    for (auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L"\"";
}
std::filesystem::path AdapterDirectory() {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, 32768)) throw std::runtime_error("Cannot locate engine");
    auto base = std::filesystem::path(path).parent_path();
    for (int level = 0; level < 4; ++level, base = base.parent_path()) {
        for (auto name : {L"Tools", L"tools"}) {
            auto directory = base / name / L"NativeSpatial";
            if (std::filesystem::exists(directory / L"native_host.py")) return directory;
        }
    }
    throw std::runtime_error("Native Spatial adapter is missing; republish the complete application");
}
class CaptureProcess {
public:
    WinHandle process, input;
    WinHandle available{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    std::thread reader;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<NativePacket> packets;
    std::atomic<bool> stopping{false};
    bool ended{};
    std::string error;
    CaptureProcess(const std::filesystem::path& adapter,
                   const std::filesystem::path& report, double seconds) {
        const auto python = adapter / L"python" / L"python.exe";
        if (!std::filesystem::exists(python)) throw std::runtime_error("Packaged Native Spatial Python runtime is missing");
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE read{}, write{};
        if (!CreatePipe(&read, &write, &security, 0)) throw std::runtime_error("Cannot create object transport pipe");
        input = WinHandle(read); WinHandle output(write);
        if (!SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("Pipe handle setup failed");
        WinHandle nullInput(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
        HANDLE err{};
        if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &err,
                             0, TRUE, DUPLICATE_SAME_ACCESS)) throw std::runtime_error("Cannot inherit diagnostic output");
        WinHandle errorOutput(err);
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nullInput.Get(); startup.StartupInfo.hStdOutput = write;
        startup.StartupInfo.hStdError = err;
        SIZE_T size{}; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<BYTE> attributes(size);
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) throw std::runtime_error("Cannot prepare capture process");
        HANDLE allowed[]{nullInput.Get(), write, err};
        if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       allowed, sizeof(allowed), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(startup.lpAttributeList); throw std::runtime_error("Cannot isolate inherited handles");
        }
        std::wstring command = Quote(python.wstring()) + L" -u " + Quote((adapter / L"native_host.py").wstring()) +
            L" --parent-pid " + std::to_wstring(GetCurrentProcessId()) + L" --seconds " + std::to_wstring(seconds) +
            L" --report " + Quote(report.wstring());
        PROCESS_INFORMATION child{};
        const BOOL created = CreateProcessW(python.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, adapter.c_str(), &startup.StartupInfo, &child);
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        if (!created) throw std::runtime_error("Cannot launch Native Spatial capture process");
        process = WinHandle(child.hProcess); CloseHandle(child.hThread);
        output = WinHandle();
        reader = std::thread([this] { ReadLoop(); });
    }
    bool ReadExact(void* destination, std::size_t bytes, bool allowEof = false) {
        auto ptr = static_cast<BYTE*>(destination); std::size_t done = 0;
        while (done < bytes && !stopping) {
            DWORD read{};
            if (!ReadFile(input.Get(), ptr + done, static_cast<DWORD>(bytes - done), &read, nullptr) || !read) {
                if (stopping || (allowEof && !done && GetLastError() == ERROR_BROKEN_PIPE)) return false;
                throw std::runtime_error("Native capture pipe ended inside a packet");
            }
            done += read;
        }
        return done == bytes;
    }
    void ReadLoop() {
        AudioScheduling scheduling(false);
        try {
            while (!stopping) {
                NativePacketHeader h;
                if (!ReadExact(&h, sizeof(h), true)) break;
                ValidateNativeHeader(h);
                std::vector<std::uint8_t> bytes(h.bytes);
                if (!ReadExact(bytes.data(), bytes.size())) break;
                auto packet = DecodeNativePacket(h, bytes);
                std::unique_lock lock(mutex);
                // Backpressure is bounded. The live producer independently reports overflow.
                changed.wait(lock, [this] { return packets.size() < 64 || stopping; });
                if (stopping) break;
                packets.push_back(std::move(packet)); changed.notify_all();SetEvent(available.Get());
            }
        } catch (const std::exception& e) { std::lock_guard lock(mutex); error = e.what(); }
        std::lock_guard lock(mutex); ended = true; changed.notify_all();SetEvent(available.Get());
    }
    bool Pop(NativePacket& packet) {
        std::lock_guard lock(mutex);
        if (!error.empty()) throw std::runtime_error(error);
        if (packets.empty()) return false;
        packet = std::move(packets.front()); packets.pop_front(); changed.notify_all(); return true;
    }
    bool Finished() { std::lock_guard lock(mutex); return ended && packets.empty(); }
    DWORD ExitCode() {
        if (WaitForSingleObject(process.Get(), 5000) != WAIT_OBJECT_0) throw std::runtime_error("Native capture cleanup timed out");
        DWORD code{}; GetExitCodeProcess(process.Get(), &code); return code;
    }
    ~CaptureProcess() {
        // Let the agent restore its hook before considering process teardown.
        SetEvent(BridgeStopEvent());
        stopping = true; changed.notify_all();
        if (reader.joinable()) { CancelSynchronousIo(reader.native_handle()); reader.join(); }
        input = WinHandle(); // Break a stalled pipe writer, while its capture control thread cleans up.
        if (process.Get()) WaitForSingleObject(process.Get(), 6000);
    }
};
