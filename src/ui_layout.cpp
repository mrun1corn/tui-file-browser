#include "ui_layout.hpp"
#include "previewer.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace ftxui;

std::vector<std::string> get_drives() {
    std::vector<std::string> drives;
#ifdef _WIN32
    DWORD drive_mask = GetLogicalDrives();
    for (char c = 'A'; c <= 'Z'; ++c) {
        if (drive_mask & 1) {
            drives.push_back(std::string(1, c) + ":\\");
        }
        drive_mask >>= 1;
    }
#else
    drives.push_back("/");
#endif
    return drives;
}

void run_ui(std::shared_ptr<AppState> state, std::shared_ptr<SearchEngine> search_engine) {
    auto screen = ScreenInteractive::Fullscreen();
    state->drives = get_drives();
    
    // Components
    auto drive_menu = Menu(&state->drives, &state->selected_drive_index);
    
    // We need a vector of strings for the file menu
    std::vector<std::string> file_menu_entries;
    auto file_menu = Menu(&file_menu_entries, &state->selected_file_index);
    

    InputOption search_option;
    search_option.on_change = [&] {
        search_engine->update_search();
    };
    auto search_input = Input(&state->search_query, "Type to search...", search_option);
    auto main_container = Container::Vertical({
        Container::Horizontal({
            drive_menu,
            file_menu
        }),
        search_input
    });

    int left_size = 20;
    int mid_size = 40;

    auto renderer = Renderer(main_container, [&] {
        // Sync files list from state
        {
            std::lock_guard<std::mutex> lock(state->data_mutex);
            file_menu_entries.clear();
            for (const auto& p : state->current_files) {
                file_menu_entries.push_back(p.filename().string());
            }
        }
        
        if (state->selected_file_index >= file_menu_entries.size()) {
            state->selected_file_index = std::max(0, (int)file_menu_entries.size() - 1);
        }

        // Update preview
        if (!state->current_files.empty() && state->selected_file_index >= 0 && state->selected_file_index < state->current_files.size()) {
            std::filesystem::path selected = state->current_files[state->selected_file_index];
            state->preview_content = Previewer::generate_preview(selected);
        } else {
            state->preview_content = "";
        }

        auto drive_win = window(text(" Drives "), drive_menu->Render() | vscroll_indicator | frame);
        auto file_win = window(text(" Files "), file_menu->Render() | vscroll_indicator | frame);
        auto preview_win = window(text(" Preview "), paragraph(state->preview_content) | vscroll_indicator | frame);
        
        auto search_win = window(text(" Search "), search_input->Render());

        auto top_split = hbox({
            drive_win | size(WIDTH, EQUAL, left_size),
            file_win | size(WIDTH, EQUAL, mid_size),
            preview_win | flex
        }) | flex;

        return vbox({
            top_split,
            search_win | size(HEIGHT, EQUAL, 3)
        });
    });

    // Handle global events (Tab to switch panes, Right to enter, etc.)
    auto event_handler = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Tab) {
            state->active_pane = (state->active_pane + 1) % 3; // 0: drives, 1: files, 2: search
            return true;
        }
        
        if (event == Event::Return || event == Event::ArrowRight) {
            if (state->active_pane == 0) { // Drives
                std::string selected_drive = state->drives[state->selected_drive_index];
                search_engine->set_root(selected_drive);
                state->active_pane = 1; // Move focus to files
                return true;
            } else if (state->active_pane == 1) { // Files
                if (!state->current_files.empty() && state->selected_file_index < state->current_files.size()) {
                    auto selected = state->current_files[state->selected_file_index];
                    if (std::filesystem::is_directory(selected)) {
                        search_engine->set_root(selected);
                        state->search_query = ""; // Reset search
                        return true;
                    }
                }
            }
        }
        
        if (event == Event::ArrowLeft) {
            if (state->active_pane == 1) {
                state->active_pane = 0;
                return true;
            }
        }

        return false; // event not fully handled, let components have it
    });

    // Custom focus logic based on active_pane
    auto focus_manager = Renderer(event_handler, [&] {
        if (state->active_pane == 0) {
            main_container->SetActiveChild(drive_menu); // doesn't strictly work like this in FTXUI 5.0, need a better way.
        } else if (state->active_pane == 1) {
            main_container->SetActiveChild(file_menu);
        } else {
            main_container->SetActiveChild(search_input);
        }
        return event_handler->Render();
    });

    // Start UI
    screen.Loop(focus_manager);
}
