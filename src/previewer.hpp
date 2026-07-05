#pragma once
#include <string>
#include <filesystem>
#include <functional>
#include <ftxui/dom/elements.hpp>

class Previewer {
public:
    static ftxui::Element generate_preview(const std::filesystem::path& path, bool& is_image, std::function<void()> redraw_cb = nullptr);

private:
    static ftxui::Element preview_text(const std::filesystem::path& path);
    static ftxui::Element preview_pdf(const std::filesystem::path& path);
    static ftxui::Element preview_directory(const std::filesystem::path& path);
    static ftxui::Element preview_image(const std::filesystem::path& path);
    static ftxui::Element preview_video(const std::filesystem::path& path, std::function<void()> redraw_cb);
    static ftxui::Element preview_audio(const std::filesystem::path& path);
};
