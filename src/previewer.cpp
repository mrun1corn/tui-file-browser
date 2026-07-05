#include "logger.hpp"
#include "previewer.hpp"
#include <fstream>
#include <iostream>
#include <array>
#include <memory>
#include <algorithm>
#include <sstream>
#include <ftxui/component/component.hpp>
#include <ftxui/screen/screen.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

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

class ImageElement : public ftxui::Node {
public:
    ImageElement(int w, int h, std::vector<unsigned char> data) 
        : width(w), height(h), img_data(std::move(data)) {}

    void ComputeRequirement() override {
        requirement_.min_x = 10;
        requirement_.min_y = 10;
        requirement_.flex_grow_x = 1;
        requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = 1;
        requirement_.flex_shrink_y = 1;
    }

    void Render(ftxui::Screen& screen) override {
        int avail_w = box_.x_max - box_.x_min + 1;
        int avail_h = box_.y_max - box_.y_min + 1;
        if (avail_w <= 0 || avail_h <= 0) return;

        if (avail_w != cached_avail_w || avail_h != cached_avail_h) {
            cached_avail_w = avail_w;
            cached_avail_h = avail_h;
            
            float scale_w = (float)avail_w / width;
            float scale_h = (float)(avail_h * 2) / height;
            float scale = std::min(scale_w, scale_h);
            
            draw_w = std::max(1, (int)(width * scale));
            draw_h = std::max(1, (int)(height * scale));
            draw_h_chars = (draw_h + 1) / 2;
            int target_h_pixels = draw_h_chars * 2; // ensure even height for half-blocks

            cached_colors.clear();
            cached_colors.reserve(draw_w * draw_h_chars);

            // Use high-quality resizing library
            std::vector<unsigned char> resized_data(draw_w * target_h_pixels * 3);
            stbir_resize_uint8_linear(img_data.data(), width, height, 0,
                                      resized_data.data(), draw_w, target_h_pixels, 0,
                                      STBIR_RGB);

            for (int y = 0; y < draw_h_chars; ++y) {
                for (int x = 0; x < draw_w; ++x) {
                    int idx1 = (y * 2 * draw_w + x) * 3;
                    int idx2 = ((y * 2 + 1) * draw_w + x) * 3;

                    auto color1 = ftxui::Color::RGB(resized_data[idx1], resized_data[idx1+1], resized_data[idx1+2]);
                    auto color2 = ftxui::Color::RGB(resized_data[idx2], resized_data[idx2+1], resized_data[idx2+2]);

                    cached_colors.push_back({color1, color2});
                }
            }
        }

        int offset_x = box_.x_min + (avail_w - draw_w) / 2;
        int offset_y = box_.y_min + (avail_h - draw_h_chars) / 2;

        int i = 0;
        for (int y = 0; y < draw_h_chars; ++y) {
            for (int x = 0; x < draw_w; ++x) {
                auto& pixel = screen.PixelAt(offset_x + x, offset_y + y);
                pixel.character = "▀";
                pixel.foreground_color = cached_colors[i].first;
                pixel.background_color = cached_colors[i].second;
                i++;
            }
        }
    }

private:
    int width, height;
    std::vector<unsigned char> img_data;
    
    int cached_avail_w = 0;
    int cached_avail_h = 0;
    int draw_w = 0, draw_h = 0, draw_h_chars = 0;
    std::vector<std::pair<ftxui::Color, ftxui::Color>> cached_colors;
private:
};

ftxui::Element Previewer::preview_image(const std::filesystem::path& path) {
    int width, height, channels;
    unsigned char* data = stbi_load(path.u8string().c_str(), &width, &height, &channels, 3);
    if (!data) {
        return ftxui::text("Failed to load image");
    }

    std::vector<unsigned char> img_vec(data, data + (width * height * 3));
    stbi_image_free(data);
    
    auto img_node = std::make_shared<ImageElement>(width, height, std::move(img_vec));
    auto info = ftxui::text("Image: " + std::to_string(width) + "x" + std::to_string(height));
    return ftxui::vbox({info, ftxui::separatorEmpty(), ftxui::Element(img_node)});
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
