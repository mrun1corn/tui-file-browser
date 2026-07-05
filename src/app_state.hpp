#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <filesystem>
#include <atomic>

struct AppState {
    std::string search_query = "";
    
    // UI state
    int active_pane = 0; // 0: Drives, 1: Files, 2: Preview
    int selected_drive_index = 0;
    int selected_file_index = 0;
    
    // Data
    std::vector<std::string> drives;
    
    std::filesystem::path current_path = "";
    std::vector<std::filesystem::path> current_files; // Filtered files for pane 2
    
    std::string preview_content = "";
    
    // Threading
    std::mutex data_mutex;
    std::atomic<bool> should_quit{false};
};
