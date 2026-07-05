#pragma once
#include "app_state.hpp"
#include <thread>
#include <vector>
#include <filesystem>
#include <atomic>
#include <memory>

class SearchEngine {
public:
    SearchEngine(std::shared_ptr<AppState> state);
    ~SearchEngine();

    void set_root(const std::filesystem::path& root);
    void update_search();

private:
    void worker_thread();

    std::shared_ptr<AppState> state;
    std::vector<std::filesystem::path> path_cache;
    
    std::filesystem::path current_root;
    std::atomic<bool> root_changed{false};
    std::atomic<bool> stop_thread{false};
    std::thread indexer;
    std::mutex cache_mutex;
};
