#include "logger.hpp"
#include "previewer.hpp"
#include <fstream>
#include <iostream>
#include <array>
#include <memory>
#include <algorithm>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

bool is_binary_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    char buffer[1024];
    file.read(buffer, sizeof(buffer));
    std::streamsize bytes_read = file.gcount();
    for (std::streamsize i = 0; i < bytes_read; ++i) {
        if (buffer[i] == '\0') return true;
    }
    return false;
}

ftxui::Element Previewer::generate_preview(const std::filesystem::path& path) {
    LOG("generate_preview called for: " + path.u8string());
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        LOG("File does not exist or access denied.");
        return ftxui::text("File does not exist or access denied.");
    }

    if (std::filesystem::is_directory(path, ec)) {
        return preview_directory(path);
    }

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif") {
        return preview_image(path);
    }

    if (ext == ".pdf") {
        return preview_pdf(path);
    } 
    
    LOG("Checking if binary...");
    if (is_binary_file(path)) {
        LOG("File is binary.");
        return ftxui::text("Binary file (Preview not supported)");
    }

    LOG("File is text. Reading first 100 lines...");
    return preview_text(path);
}

ftxui::Element Previewer::preview_text(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return ftxui::text("Unable to open file for reading.");

    ftxui::Elements lines;
    std::string line;
    int line_count = 0;
    while (std::getline(file, line) && line_count < 100) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(ftxui::text(line));
        line_count++;
    }
    
    if (std::getline(file, line)) {
        lines.push_back(ftxui::text("... (truncated)"));
    }

    return ftxui::vbox(std::move(lines));
}

ftxui::Element Previewer::preview_pdf(const std::filesystem::path& path) {
#ifdef _WIN32
    std::string cmd = "pdftotext \"" + path.string() + "\" - 2>nul";
#else
    std::string cmd = "pdftotext \"" + path.string() + "\" - 2>/dev/null";
#endif
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return ftxui::text("PDF extraction failed.");
    }
    
    ftxui::Elements lines;
    std::array<char, 256> buffer;
    std::string result;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
        if (result.size() > 1024 * 10) { // Limit
            result += "\n... (truncated)";
            break;
        }
    }
    
    if (result.empty()) {
        return ftxui::text("PDF Binary Data or failed to extract text.");
    }

    std::stringstream ss(result);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(ftxui::text(line));
    }

    return ftxui::vbox(std::move(lines));
}

ftxui::Element Previewer::preview_image(const std::filesystem::path& path) {
    int width, height, channels;
    unsigned char* data = stbi_load(path.u8string().c_str(), &width, &height, &channels, 3);
    if (!data) {
        return ftxui::text("Failed to load image");
    }

    int max_w = 80;
    int max_h = 40;
    float scale = 1.0f;
    if (width > max_w || height > max_h * 2) {
        float scale_w = (float)max_w / width;
        float scale_h = (float)(max_h * 2) / height;
        scale = std::min(scale_w, scale_h);
    }

    int scaled_w = std::max(1, (int)(width * scale));
    int scaled_h = std::max(1, (int)(height * scale));

    ftxui::Elements rows;
    for (int y = 0; y < scaled_h; y += 2) {
        ftxui::Elements cols;
        for (int x = 0; x < scaled_w; ++x) {
            int src_x = std::min((int)(x / scale), width - 1);
            int src_y1 = std::min((int)(y / scale), height - 1);
            int src_y2 = std::min((int)((y + 1) / scale), height - 1);

            int idx1 = (src_y1 * width + src_x) * 3;
            int idx2 = (src_y2 * width + src_x) * 3;

            auto color1 = ftxui::Color::RGB(data[idx1], data[idx1+1], data[idx1+2]);
            auto color2 = ftxui::Color::RGB(data[idx2], data[idx2+1], data[idx2+2]);

            cols.push_back(ftxui::text("▀") | ftxui::color(color1) | ftxui::bgcolor(color2));
        }
        rows.push_back(ftxui::hbox(std::move(cols)));
    }

    stbi_image_free(data);
    
    // Add file info header
    auto info = ftxui::text("Image: " + std::to_string(width) + "x" + std::to_string(height));
    return ftxui::vbox({info, ftxui::separatorEmpty(), ftxui::vbox(std::move(rows))});
}

ftxui::Element Previewer::preview_directory(const std::filesystem::path& path) {
    int item_count = 0;
    try {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            item_count++;
            if (item_count > 1000) break;
        }
    } catch (...) {
        return ftxui::text("Directory (Access Denied)");
    }
    return ftxui::vbox({
        ftxui::text("Directory"),
        ftxui::text("Contains approx " + std::to_string(item_count) + " items.")
    });
}
