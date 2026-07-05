#include "src/process_manager.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <cassert>
#include <windows.h>
#include <tlhelp32.h>

// Helper to check if a process with a given name is running in Windows
bool is_process_running(const std::wstring& process_name) {
    bool exists = false;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (std::wstring(entry.szExeFile) == process_name) {
                exists = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return exists;
}

int main() {
    std::cout << "Starting ProcessManager lifecycle test..." << std::endl;

    std::wstring ffmpeg_wname = L"ffmpeg.exe";
    
    // Check if any ffmpeg is already running
    if (is_process_running(ffmpeg_wname)) {
        std::cout << "Warning: ffmpeg.exe is already running before the test." << std::endl;
    }

    std::vector<std::thread> threads;
    std::string ffmpeg_bin = "C:\\Users\\Robin\\AppData\\Local\\Microsoft\\WinGet\\Links\\ffmpeg.exe";

    std::cout << "Spawning 20 ffmpeg processes in separate threads..." << std::endl;
    for (int i = 0; i < 20; ++i) {
        threads.emplace_back([ffmpeg_bin, i]() {
            std::string cmd = "\"" + ffmpeg_bin + "\" -y -ss 00:00:01 -i test.mp4 -vframes 1 -f image2 thumb_out_" + std::to_string(i) + ".jpg -loglevel quiet";
            bool success = ProcessManager::get_instance().spawn_sync(cmd);
            std::cout << "Thread " << i << " finished. Success: " << (success ? "yes" : "no") << std::endl;
        });
    }

    // Wait a brief moment to let them all start and register
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Calling kill_active_previews() to terminate all running previews..." << std::endl;
    ProcessManager::get_instance().kill_active_previews();

    std::cout << "Joining all threads..." << std::endl;
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    std::cout << "All threads joined. Waiting a short bit for OS cleanup..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Check if any ffmpeg.exe is still running
    bool running = is_process_running(ffmpeg_wname);
    std::cout << "Are any ffmpeg.exe processes still running? " << (running ? "YES (FAIL)" : "NO (PASS)") << std::endl;

    if (running) {
        return 1; // Failure
    } else {
        std::cout << "Test PASSED! No orphaned processes left." << std::endl;
        return 0; // Success
    }
}
