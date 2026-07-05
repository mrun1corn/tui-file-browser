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

std::string find_chafa_path() {
    std::error_code ec;
    if (std::filesystem::exists("chafa.exe", ec)) return "chafa.exe";
    if (std::filesystem::exists("build/Release/chafa.exe", ec)) return "build/Release/chafa.exe";
    if (std::filesystem::exists("build/chafa.exe", ec)) return "build/chafa.exe";
    if (std::filesystem::exists("chafa", ec)) return "./chafa";
    return "chafa"; // Fallback to system PATH
}

struct CustomPixel {
    std::string character = " ";
    ftxui::Color fg = ftxui::Color::Default;
    ftxui::Color bg = ftxui::Color::Default;
};

class ImageElement : public ftxui::Node {
public:
    ImageElement(int w, int h, std::vector<unsigned char> data, std::filesystem::path p) 
        : width(w), height(h), img_data(std::move(data)), path(std::move(p)) {}

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
            chafa_success = false;

            std::string chafa_bin = find_chafa_path();
            LOG("Chafa path resolved to: " + chafa_bin);

            // Try running chafa first
#ifdef _WIN32
            std::string cmd = chafa_bin + " --colors 24 --symbols vhalf+quad -s " + std::to_string(avail_w) + "x" + std::to_string(avail_h) + " \"" + path.string() + "\" 2>nul";
#else
            std::string cmd = chafa_bin + " --colors 24 --symbols vhalf+quad -s " + std::to_string(avail_w) + "x" + std::to_string(avail_h) + " \"" + path.string() + "\" 2>/dev/null";
#endif
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                std::vector<std::vector<CustomPixel>> temp_grid;
                std::vector<CustomPixel> current_row;
                ftxui::Color fg = ftxui::Color::Default;
                ftxui::Color bg = ftxui::Color::Default;
                
                char buf[1024];
                std::string output;
                while (fgets(buf, sizeof(buf), pipe) != nullptr) {
                    output += buf;
                }
                pclose(pipe);

                if (!output.empty()) {
                    chafa_success = true;
                    LOG("Chafa execution SUCCESS. Output size: " + std::to_string(output.size()));
                    size_t i = 0;
                    while (i < output.size()) {
                        if (output[i] == '\033' && i + 1 < output.size() && output[i+1] == '[') {
                            i += 2;
                            std::string code;
                            while (i < output.size() && output[i] != 'm') {
                                code += output[i];
                                i++;
                            }
                            i++; // skip 'm'
                            
                            std::stringstream ss(code);
                            std::string token;
                            std::vector<int> vals;
                            while (std::getline(ss, token, ';')) {
                                try { vals.push_back(std::stoi(token)); } catch(...) {}
                            }
                            if (vals.empty()) continue;
                            
                            if (vals[0] == 0) {
                                fg = ftxui::Color::Default;
                                bg = ftxui::Color::Default;
                            } else if (vals[0] == 39) {
                                fg = ftxui::Color::Default;
                            } else if (vals[0] == 49) {
                                bg = ftxui::Color::Default;
                            } else if (vals[0] == 38 && vals.size() >= 5 && vals[1] == 2) {
                                fg = ftxui::Color::RGB(vals[2], vals[3], vals[4]);
                            } else if (vals[0] == 48 && vals.size() >= 5 && vals[1] == 2) {
                                bg = ftxui::Color::RGB(vals[2], vals[3], vals[4]);
                            }
                        } else if (output[i] == '\n') {
                            temp_grid.push_back(std::move(current_row));
                            current_row = std::vector<CustomPixel>();
                            i++;
                        } else {
                            unsigned char c = output[i];
                            size_t len = 1;
                            if ((c & 0x80) == 0) len = 1;
                            else if ((c & 0xE0) == 0xC0) len = 2;
                            else if ((c & 0xF0) == 0xE0) len = 3;
                            else if ((c & 0xF8) == 0xF0) len = 4;
                            
                            if (i + len <= output.size()) {
                                std::string utf8_char = output.substr(i, len);
                                current_row.push_back({utf8_char, fg, bg});
                                i += len;
                            } else {
                                i++;
                            }
                        }
                    }
                    if (!current_row.empty()) {
                        temp_grid.push_back(std::move(current_row));
                    }
                    cached_pixels = std::move(temp_grid);
                }
            }

            // Fallback to stb_image_resize if chafa failed or isn't installed
            if (!chafa_success) {
                LOG("Chafa execution FAILED. Falling back to stb_image_resize.");
                float scale_w = (float)avail_w / width;
                float scale_h = (float)(avail_h * 2) / height;
                float scale = std::min(scale_w, scale_h);
                
                draw_w = std::max(1, (int)(width * scale));
                draw_h = std::max(1, (int)(height * scale));
                draw_h_chars = (draw_h + 1) / 2;
                int target_h_pixels = draw_h_chars * 2;

                cached_colors.clear();
                cached_colors.reserve(draw_w * draw_h_chars);

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
        }

        if (chafa_success) {
            int draw_h_chars = cached_pixels.size();
            int draw_w = draw_h_chars > 0 ? cached_pixels[0].size() : 0;
            int offset_x = box_.x_min + (avail_w - draw_w) / 2;
            int offset_y = box_.y_min + (avail_h - draw_h_chars) / 2;
            
            for (int y = 0; y < draw_h_chars && y + offset_y <= box_.y_max; ++y) {
                for (int x = 0; x < (int)cached_pixels[y].size() && x + offset_x <= box_.x_max; ++x) {
                    auto& pixel = screen.PixelAt(offset_x + x, offset_y + y);
                    pixel.character = cached_pixels[y][x].character;
                    pixel.foreground_color = cached_pixels[y][x].fg;
                    pixel.background_color = cached_pixels[y][x].bg;
                }
            }
        } else {
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
    }

private:
    int width, height;
    std::vector<unsigned char> img_data;
    std::filesystem::path path;
    
    int cached_avail_w = 0;
    int cached_avail_h = 0;
    int draw_w = 0, draw_h = 0, draw_h_chars = 0;
    bool chafa_success = false;
    std::vector<std::pair<ftxui::Color, ftxui::Color>> cached_colors;
    std::vector<std::vector<CustomPixel>> cached_pixels;
};

ftxui::Element Previewer::generate_preview(const std::filesystem::path& path, bool& is_image) {
    is_image = false; // default
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
        is_image = true;
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

    std::vector<unsigned char> img_vec(data, data + (width * height * 3));
    stbi_image_free(data);
    
    auto img_node = std::make_shared<ImageElement>(width, height, std::move(img_vec), path);
    auto info = ftxui::text("Image: " + std::to_string(width) + "x" + std::to_string(height));
    return ftxui::vbox({info, ftxui::separatorEmpty(), ftxui::Element(img_node) | ftxui::flex}) | ftxui::flex;
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
