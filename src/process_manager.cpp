#include "process_manager.hpp"
#include "logger.hpp"
#include <iostream>
#include <sstream>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

ProcessManager& ProcessManager::get_instance() {
    static ProcessManager instance;
    return instance;
}

ProcessManager::~ProcessManager() {
    cleanup();
}

bool ProcessManager::spawn(const std::string& cmd, bool is_preview) {
    std::lock_guard<std::mutex> lock(mtx);
    LOG("ProcessManager: spawning cmd: " + cmd);

#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::string cmd_copy = cmd;
    DWORD creation_flags = CREATE_NO_WINDOW;
    
    if (cmd.find("-nodisp") == std::string::npos && (cmd.find("ffplay") != std::string::npos || cmd.find("mpv") != std::string::npos)) {
        creation_flags = 0; // Show normal window
    }

    if (CreateProcessA(NULL, &cmd_copy[0], NULL, NULL, FALSE, creation_flags, NULL, NULL, &si, &pi)) {
        LOG("ProcessManager: Successfully spawned process with PID " + std::to_string(pi.dwProcessId));
        if (is_preview) {
            ProcessInfo info;
            info.hProcess = pi.hProcess;
            info.hThread = pi.hThread;
            info.dwProcessId = pi.dwProcessId;
            info.is_sync = false;
            active_previews.push_back(info);
        } else {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        return true;
    } else {
        LOG("ProcessManager: Failed to spawn process. Error: " + std::to_string(GetLastError()));
        return false;
    }
#else
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<std::string> args;
        std::stringstream ss(cmd);
        std::string arg;
        while (ss >> arg) {
            args.push_back(arg);
        }
        std::vector<char*> cargs;
        for (auto& a : args) {
            cargs.push_back(&a[0]);
        }
        cargs.push_back(nullptr);

        execvp(cargs[0], cargs.data());
        exit(127);
    } else if (pid > 0) {
        LOG("ProcessManager: Spawned Unix process PID " + std::to_string(pid));
        if (is_preview) {
            active_previews.push_back(pid);
        }
        return true;
    }
    return false;
#endif
}

bool ProcessManager::spawn_sync(const std::string& cmd) {
    LOG("ProcessManager: spawning sync cmd: " + cmd);
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::string cmd_copy = cmd;
    if (CreateProcessA(NULL, &cmd_copy[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        LOG("ProcessManager: Waiting for sync process " + std::to_string(pi.dwProcessId) + " to finish...");
        
        ProcessInfo info;
        info.hProcess = pi.hProcess;
        info.hThread = pi.hThread;
        info.dwProcessId = pi.dwProcessId;
        
        {
            std::lock_guard<std::mutex> lock(mtx);
            active_previews.push_back(info);
        }

        DWORD wait_result = WaitForSingleObject(pi.hProcess, INFINITE);

        std::lock_guard<std::mutex> lock(mtx);
        auto it = std::find_if(active_previews.begin(), active_previews.end(), [&](const ProcessInfo& p) {
            return p.hProcess == pi.hProcess;
        });

        if (it != active_previews.end()) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            active_previews.erase(it);
            LOG("ProcessManager: Sync process finished normally and handles closed.");
        } else {
            LOG("ProcessManager: Sync process was terminated externally; handles already closed.");
        }
        return wait_result == WAIT_OBJECT_0;
    }
    LOG("ProcessManager: Failed to spawn sync process. Error: " + std::to_string(GetLastError()));
    return false;
#else
    int status = std::system(cmd.c_str());
    return status == 0;
#endif
}

void ProcessManager::kill_active_previews() {
    std::lock_guard<std::mutex> lock(mtx);
    if (active_previews.empty()) return;

    LOG("ProcessManager: Killing " + std::to_string(active_previews.size()) + " active previews");

#ifdef _WIN32
    std::vector<ProcessInfo> sync_to_keep;
    for (auto& info : active_previews) {
        if (info.hProcess != NULL) {
            LOG("ProcessManager: Terminating PID " + std::to_string(info.dwProcessId));
            TerminateProcess(info.hProcess, 0);
            if (!info.is_sync) {
                CloseHandle(info.hProcess);
                CloseHandle(info.hThread);
            } else {
                sync_to_keep.push_back(info);
            }
        }
    }
    active_previews = sync_to_keep;
#else
    for (int pid : active_previews) {
        LOG("ProcessManager: Sending SIGTERM to PID " + std::to_string(pid));
        kill(pid, SIGTERM);
        int status;
        waitpid(pid, &status, WNOHANG);
    }
    active_previews.clear();
#endif
}

bool ProcessManager::has_active_previews() {
    std::lock_guard<std::mutex> lock(mtx);
    return !active_previews.empty();
}

void ProcessManager::cleanup() {
    kill_active_previews();
}
