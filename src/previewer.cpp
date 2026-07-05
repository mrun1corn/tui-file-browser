#include "previewer.hpp"
#include <fstream>
#include <iostream>
#include <array>
#include <memory>
#include <algorithm>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

std::string Previewer::generate_preview(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return "File does not exist or access denied.";

    if (std::filesystem::is_directory(path, ec)) {
        return preview_directory(path);
    }

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".pdf") {
        return preview_pdf(path);
    } 
    // Fallback for most things is attempting to read as text (first 100 lines)
    return preview_text(path);
}

std::string Previewer::preview_text(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "Unable to open file for reading.";

    std::string content;
    std::string line;
    int line_count = 0;
    while (std::getline(file, line) && line_count < 100) {
        content += line + "\n";
        line_count++;
    }
    
    if (std::getline(file, line)) {
        content += "\n... (truncated)";
    }

    return content;
}
std::string Previewer::preview_pdf(const std::filesystem::path& path) {
#ifdef _WIN32
    std::string cmd = "pdftotext \"" + path.string() + "\" - 2>nul";
#else
    std::string cmd = "pdftotext \"" + path.string() + "\" - 2>/dev/null";
#endif
    std::string result = "";
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return "PDF Binary Data (Install pdftotext to view or failed to run)";
    }
    
    std::array<char, 128> buffer;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
        if (result.size() > 1024 * 10) { // Limit to 10KB of text
            result += "\n... (truncated)";
            break;
        }
    }
    
    if (result.empty()) {
        return "PDF Binary Data (Install pdftotext to view)";
    }
    return result;
}

std::string Previewer::preview_directory(const std::filesystem::path& path) {
    int item_count = 0;
    try {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            item_count++;
            if (item_count > 1000) break; // Don't count forever
        }
    } catch (...) {
        return "Directory (Access Denied)";
    }
    return "Directory\nContains approx " + std::to_string(item_count) + " items.";
}
