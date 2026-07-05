#pragma once
#include <string>
#include <filesystem>
#include <ftxui/dom/elements.hpp>

class Previewer {
public:
    static ftxui::Element generate_preview(const std::filesystem::path& path);

private:
    static ftxui::Element preview_text(const std::filesystem::path& path);
    static ftxui::Element preview_pdf(const std::filesystem::path& path);
    static ftxui::Element preview_directory(const std::filesystem::path& path);
    static ftxui::Element preview_image(const std::filesystem::path& path);
};
