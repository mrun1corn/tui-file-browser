#pragma once
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>

class Logger {
public:
    static void init(const std::string& filename) {
        get_instance().file.open(filename, std::ios::app);
    }
    static void log(const std::string& msg) {
        auto& inst = get_instance();
        std::lock_guard<std::mutex> lock(inst.m);
        if (inst.file.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            inst.file << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << " - " << msg << std::endl;
            inst.file.flush();
        }
    }
private:
    Logger() = default;
    static Logger& get_instance() {
        static Logger instance;
        return instance;
    }
    std::ofstream file;
    std::mutex m;
};

#define LOG(msg) Logger::log(msg)
