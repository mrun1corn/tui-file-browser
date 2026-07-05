#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <memory>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

class ProcessManager {
public:
    static ProcessManager& get_instance();

    // Spawn a process.
    // cmd: Command line to execute
    // is_preview: If true, this process is tracked as a preview task and killed when navigating
    bool spawn(const std::string& cmd, bool is_preview = true);

    // Spawn a process synchronously and block the calling thread until it exits
    bool spawn_sync(const std::string& cmd);

    // Terminate all currently active preview processes (e.g. ffmpeg extraction or ffplay audio)
    void kill_active_previews();
    // Check if any previews are active
    bool has_active_previews();

    // Clean up everything on exit
    void cleanup();

private:
    ProcessManager() = default;
    ~ProcessManager();
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    std::mutex mtx;

#ifdef _WIN32
    struct ProcessInfo {
        HANDLE hProcess = NULL;
        HANDLE hThread = NULL;
        DWORD dwProcessId = 0;
        bool is_sync = false;
    };
    std::vector<ProcessInfo> active_previews;
#else
    std::vector<int> active_previews; // PIDs for Unix
#endif
};
