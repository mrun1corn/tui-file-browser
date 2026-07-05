cmake_minimum_required(VERSION 3.14)
project(TuiFileBrowser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG v5.0.0
)

FetchContent_MakeAvailable(ftxui)

add_executable(tui_browser 
    src/main.cpp
    src/ui_layout.cpp
    src/search_engine.cpp
    src/previewer.cpp
    src/process_manager.cpp
)
target_include_directories(tui_browser PRIVATE src)
target_link_libraries(tui_browser PRIVATE ftxui::screen ftxui::dom ftxui::component)

add_executable(test_process_manager
    test_process_manager.cpp
    src/process_manager.cpp
)
target_include_directories(test_process_manager PRIVATE src)
