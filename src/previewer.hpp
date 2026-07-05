#pragma once
#include <string>
#include <filesystem>

class Previewer {
public:
    static std::string generate_preview(const std::filesystem::path& path);

private:
    static std::string preview_text(const std::filesystem::path& path);
    static std::string preview_pdf(const std::filesystem::path& path);
    static std::string preview_directory(const std::filesystem::path& path);
};
