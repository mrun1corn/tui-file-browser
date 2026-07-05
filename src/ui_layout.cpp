#include "logger.hpp"
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
    
    auto search_engine_ptr = search_engine.get();
    search_engine->set_on_new_files([&screen, search_engine_ptr]() {
        screen.Post([search_engine_ptr] {
            search_engine_ptr->update_search();
        });
    });
    std::vector<std::string> file_menu_entries;
    auto file_menu = Menu(&file_menu_entries, &state->selected_file_index);
    

    InputOption search_option;
    search_option.on_change = [&] {
        search_engine->update_search();
    };
    auto search_input = Input(&state->search_query, "Type to search...", search_option);
    int horizontal_selector = 0;
    int vertical_selector = 0;

    auto panes = Container::Horizontal({
        drive_menu,
        file_menu
    }, &horizontal_selector);

    auto main_container = Container::Vertical({
        panes,
        search_input
    }, &vertical_selector);
    
    auto update_selectors = [&] {
        if (state->active_pane == 0) { horizontal_selector = 0; vertical_selector = 0; }
        else if (state->active_pane == 1) { horizontal_selector = 1; vertical_selector = 0; }
        else { vertical_selector = 1; }
    };
    update_selectors();


    int left_size = 20;
    int mid_size = 40;

    auto renderer = Renderer(main_container, [&] {
        update_selectors();
        // Sync files list from state
        {
            std::lock_guard<std::mutex> lock(state->data_mutex);
            file_menu_entries.clear();
            for (const auto& p : state->current_files) {
                if (state->search_query.empty()) {
                    file_menu_entries.push_back(p.filename().u8string());
                } else {
                    file_menu_entries.push_back(p.u8string());
                }
            }
        }
        
        if (state->selected_file_index >= file_menu_entries.size()) {
            state->selected_file_index = std::max(0, (int)file_menu_entries.size() - 1);
        }

        // Update preview
        if (!state->current_files.empty() && state->selected_file_index >= 0 && state->selected_file_index < state->current_files.size()) {
            std::filesystem::path selected = state->current_files[state->selected_file_index];
            LOG("Selected file changed: " + selected.u8string());
            state->preview_element = Previewer::generate_preview(selected);
            LOG("Preview content generated.");
        }

        auto drive_win = window(text(" Drives "), drive_menu->Render() | vscroll_indicator | frame);
        std::string mid_title = state->current_path.empty() ? " Files " : " " + state->current_path.u8string() + " ";
        auto file_win = window(text(mid_title), file_menu->Render() | vscroll_indicator | frame);
        auto preview_win = window(text(" Preview "), state->preview_element | vscroll_indicator | frame);
        
        auto search_win = window(text(" Search "), search_input->Render());

        auto top_split = hbox({
            drive_win | size(WIDTH, EQUAL, left_size),
            file_win | size(WIDTH, EQUAL, mid_size),
            preview_win | flex
        }) | flex;

    // Handle global events (Tab to switch panes, Right to enter, etc.)

        return vbox({
            top_split,
            search_win | size(HEIGHT, EQUAL, 3)
        });
    });
    auto event_handler = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Tab) {
            state->active_pane = (state->active_pane + 1) % 3; // 0: drives, 1: files, 2: search
            return true;
        }
        
        if (event == Event::Return || event == Event::ArrowRight) {
            if (state->active_pane == 0) { // Drives
                std::string selected_drive = state->drives[state->selected_drive_index];
                state->current_path = selected_drive;
                search_engine->set_root(selected_drive);
                state->selected_file_index = 0;
                search_engine->update_search(); // Immediate update
                state->active_pane = 1; // Move focus to files
                return true;
            } else if (state->active_pane == 1) { // Files
                if (!state->current_files.empty() && state->selected_file_index < state->current_files.size()) {
                    auto selected = state->current_files[state->selected_file_index];
                    if (std::filesystem::is_directory(selected)) {
                        state->current_path = selected;
                        search_engine->set_root(selected);
                        state->search_query = ""; // Reset search
                        state->selected_file_index = 0;
                        search_engine->update_search(); // Immediate update
                        return true;
                    }
                }
            }
        }
        
        if (event == Event::ArrowLeft) {
            if (state->active_pane == 1) {
                auto parent = state->current_path.parent_path();
                if (parent != state->current_path && state->current_path.has_parent_path()) {
                    state->current_path = parent;
                    state->selected_file_index = 0;
                    search_engine->update_search();
                } else {
                    state->active_pane = 0;
                }
                return true;
            }
        }

        return false; // event not fully handled, let components have it
    });
    // Start UI
    screen.Loop(event_handler);
}
