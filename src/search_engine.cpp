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

void SearchEngine::set_on_new_files(std::function<void()> cb) {
    on_new_files = cb;
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
    
    if (query.empty()) {
        // Folder-wise navigation: show direct children of state->current_path
        std::error_code ec;
        if (!state->current_path.empty() && std::filesystem::exists(state->current_path, ec) && std::filesystem::is_directory(state->current_path, ec)) {
            std::vector<std::filesystem::path> folders;
            std::vector<std::filesystem::path> files;
            try {
                for (const auto& entry : std::filesystem::directory_iterator(state->current_path, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (ec) break;
                    std::error_code ec_entry;
                    if (entry.is_directory(ec_entry)) folders.push_back(entry.path());
                    else files.push_back(entry.path());
                }
            } catch (...) {}
            // Sort alphabetically, folders first
            std::sort(folders.begin(), folders.end());
            std::sort(files.begin(), files.end());
            state->current_files.insert(state->current_files.end(), folders.begin(), folders.end());
            state->current_files.insert(state->current_files.end(), files.begin(), files.end());
        }
    } else {
        // Global "Everything" style fuzzy search using background cache
        for (const auto& p : path_cache) {
            if (fuzzy_match(query, p.filename().string())) {
                state->current_files.push_back(p);
            }
            // Limit to prevent UI overload
            if (state->current_files.size() > 1000) break;
        }
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

                    int current_size = 0;
                    {
                        std::lock_guard<std::mutex> lock(cache_mutex);
                        path_cache.push_back(it->path());
                        current_size = path_cache.size();
                    }
                    
                    if (current_size % 100 == 0 && on_new_files) {
                        on_new_files();
                    }
                    
                    // Yield occasionally if we want to be nice, but this is a background thread
                }
                active_root.clear(); // Done indexing this root
                if (on_new_files && !root_changed) {
                    on_new_files();
                }
            } catch (const std::filesystem::filesystem_error&) {
                // Ignore access errors
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}
