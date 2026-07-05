#include "search_engine.hpp"
#include <algorithm>
#include <cctype>

SearchEngine::SearchEngine(std::shared_ptr<AppState> state) : state(state) {
    indexer = std::thread(&SearchEngine::worker_thread, this);
}

SearchEngine::~SearchEngine() {
    stop_thread = true;
    if (indexer.joinable()) {
        indexer.join();
    }
}

void SearchEngine::set_root(const std::filesystem::path& root) {
    current_root = root;
    root_changed = true;
}

// Simple case-insensitive substring match
bool fuzzy_match(const std::string& query, const std::string& text) {
    if (query.empty()) return true;
    auto it = std::search(
        text.begin(), text.end(),
        query.begin(), query.end(),
        [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
    );
    return (it != text.end());
}

void SearchEngine::update_search() {
    std::lock_guard<std::mutex> cache_lock(cache_mutex);
    std::lock_guard<std::mutex> state_lock(state->data_mutex);
    
    state->current_files.clear();
    std::string query = state->search_query;
    
    for (const auto& p : path_cache) {
        if (fuzzy_match(query, p.filename().string())) {
            state->current_files.push_back(p);
        }
        // Limit to prevent UI overload
        if (state->current_files.size() > 1000) break;
    }
}

void SearchEngine::worker_thread() {
    std::filesystem::path active_root;
    
    while (!stop_thread) {
        if (root_changed) {
            root_changed = false;
            active_root = current_root;
            
            std::lock_guard<std::mutex> lock(cache_mutex);
            path_cache.clear();
        }
        
        if (!active_root.empty()) {
            try {
                auto opts = std::filesystem::directory_options::skip_permission_denied;
                for (auto it = std::filesystem::recursive_directory_iterator(active_root, opts);
                     it != std::filesystem::recursive_directory_iterator(); ++it) {
                    
                    if (stop_thread || root_changed) break;

                    {
                        std::lock_guard<std::mutex> lock(cache_mutex);
                        path_cache.push_back(it->path());
                    }
                    
                    // Yield occasionally if we want to be nice, but this is a background thread
                }
            } catch (const std::filesystem::filesystem_error&) {
                // Ignore access errors
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}
