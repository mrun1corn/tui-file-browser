#include "src/process_manager.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <sys/types.h>
#include <signal.h>
#endif

std::string find_ffplay_path() {
    std::error_code ec;
    if (std::filesystem::exists("ffplay.exe", ec)) return "ffplay.exe";
    if (std::filesystem::exists("build/Release/ffplay.exe", ec)) return "build/Release/ffplay.exe";
    
    char* user_profile = std::getenv("USERPROFILE");
    if (user_profile) {
        std::filesystem::path winget_link = std::filesystem::path(user_profile) / "AppData" / "Local" / "Microsoft" / "WinGet" / "Links" / "ffplay.exe";
        if (std::filesystem::exists(winget_link, ec)) {
            return "\"" + winget_link.string() + "\"";
        }
    }
    
    return "ffplay";
}

bool is_ffplay_running() {
#ifdef _WIN32
    bool exists = false;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (std::wstring(entry.szExeFile) == L"ffplay.exe") {
                exists = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return exists;
#else
    // Check using pgrep or similar in unix, or standard system call
    int ret = std::system("pgrep -x ffplay > /dev/null");
    return ret == 0;
#endif
}

int main() {
    std::cout << "Starting Playback & Process Lifecycle test..." << std::endl;

    std::string ffplay_bin = find_ffplay_path();
    std::cout << "Using ffplay binary: " << ffplay_bin << std::endl;

    // Clean up any stray ffplay processes before test
    if (is_ffplay_running()) {
        std::cout << "Warning: ffplay.exe is already running. Killing it..." << std::endl;
#ifdef _WIN32
        std::system("taskkill /f /im ffplay.exe >nul 2>&1");
#else
        std::system("killall -9 ffplay > /dev/null 2>&1");
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // 1. Audio Playback Test (is_preview = true)
    std::cout << "--- 1. Testing Audio Playback (as preview) ---" << std::endl;
    std::string audio_cmd = ffplay_bin + " -nodisp -autoexit test.wav";
    bool success_audio = ProcessManager::get_instance().spawn(audio_cmd, true);
    assert(success_audio);
    std::cout << "Spawned audio command." << std::endl;

    // Wait a brief moment
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Verify it is active in the ProcessManager and running in OS
    assert(ProcessManager::get_instance().has_active_previews());
    assert(is_ffplay_running());
    std::cout << "Verified: Audio is running in OS and tracked by ProcessManager." << std::endl;

    // Kill preview
    std::cout << "Killing active audio preview..." << std::endl;
    ProcessManager::get_instance().kill_active_previews();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Verify it is no longer active/running
    assert(!ProcessManager::get_instance().has_active_previews());
    assert(!is_ffplay_running());
    std::cout << "Verified: Audio stopped and process was terminated." << std::endl;

    // 2. Video Playback Test (is_preview = false)
    std::cout << "--- 2. Testing Video Playback (NOT as preview) ---" << std::endl;
    std::string video_cmd = ffplay_bin + " -autoexit test.mp4";
    bool success_video = ProcessManager::get_instance().spawn(video_cmd, false);
    assert(success_video);
    std::cout << "Spawned video command." << std::endl;

    // Wait a brief moment
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Verify it is running in OS, but NOT tracked as a preview in ProcessManager
    assert(!ProcessManager::get_instance().has_active_previews());
    assert(is_ffplay_running());
    std::cout << "Verified: Video is running in OS and NOT tracked as preview by ProcessManager." << std::endl;

    // Call kill_active_previews() and verify video process is NOT killed (since is_preview = false)
    std::cout << "Calling kill_active_previews() (should not affect video playback)..." << std::endl;
    ProcessManager::get_instance().kill_active_previews();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    assert(is_ffplay_running());
    std::cout << "Verified: Video is still running after kill_active_previews()." << std::endl;

    // Now clean it up
    std::cout << "Cleaning up video process..." << std::endl;
#ifdef _WIN32
    std::system("taskkill /f /im ffplay.exe >nul 2>&1");
#else
    std::system("killall -9 ffplay > /dev/null 2>&1");
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(!is_ffplay_running());

    std::cout << "Playback & Process Lifecycle test PASSED!" << std::endl;
    return 0;
}
