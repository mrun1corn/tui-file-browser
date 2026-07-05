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
class FocusablePreview : public ComponentBase {
public:
    FocusablePreview(std::shared_ptr<AppState> state, std::function<void()> on_scroll) 
        : state_(state), on_scroll_(on_scroll) {}
    bool Focusable() const override { return true; }
    
    Element Render() override {
        ftxui::Element content = state_->preview_element;
        if (!state_->is_image_preview) {
            if (Focused()) {
                content = content | focusPositionRelative(0, state_->preview_scroll / 100.0f) | ftxui::focus;
            }
            content = content | vscroll_indicator | frame;
        }
        
        return window(text(" Preview ") | (Focused() ? bold : dim), content);
    }
    
    bool OnEvent(Event event) override {
        if (!Focused()) return false;
        if (event == Event::ArrowDown || event == Event::PageDown || event == Event::Character('j')) {
            state_->preview_scroll = std::min(100, state_->preview_scroll + 10);
            if (on_scroll_) on_scroll_();
            return true;
        }
        if (event == Event::ArrowUp || event == Event::PageUp || event == Event::Character('k')) {
            state_->preview_scroll = std::max(0, state_->preview_scroll - 10);
            if (on_scroll_) on_scroll_();
            return true;
        }
        return false;
    }

private:
    std::shared_ptr<AppState> state_;
    std::function<void()> on_scroll_;
};
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

    auto preview_container = std::make_shared<FocusablePreview>(state, [&] {
        screen.Post([]{});
    });

    auto panes = Container::Horizontal({
        drive_menu,
        file_menu,
        preview_container
    }, &horizontal_selector);

    auto main_container = Container::Vertical({
        panes,
        search_input
    }, &vertical_selector);
    
    auto update_selectors = [&] {
        if (state->active_pane == 0) { horizontal_selector = 0; vertical_selector = 0; }
        else if (state->active_pane == 1) { horizontal_selector = 1; vertical_selector = 0; }
        else if (state->active_pane == 2) { horizontal_selector = 2; vertical_selector = 0; }
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
            state->preview_scroll = 0; // Reset scroll when file changes
            state->preview_element = Previewer::generate_preview(selected, state->is_image_preview);
            LOG("Preview content generated.");
        }

        auto drive_win = window(text(" Drives "), drive_menu->Render() | vscroll_indicator | frame);
        std::string mid_title = state->current_path.empty() ? " Files " : " " + state->current_path.u8string() + " ";
        auto file_win = window(text(mid_title), file_menu->Render() | vscroll_indicator | frame);
        
        auto top_split = hbox({
            drive_win | size(WIDTH, EQUAL, left_size),
            file_win | size(WIDTH, EQUAL, mid_size),
            preview_container->Render() | flex
        }) | flex;
        
        auto search_win = window(text(" Search "), search_input->Render());


    // Handle global events (Tab to switch panes, Right to enter, etc.)

        return vbox({
            top_split,
            search_win | size(HEIGHT, EQUAL, 3)
        });
    });
    auto event_handler = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Tab) {
            state->active_pane = (state->active_pane + 1) % 4; // 0: drives, 1: files, 2: preview, 3: search
            return true;
        }
        
        if (event == Event::Return) {
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
                    std::error_code ec;
                    if (std::filesystem::is_directory(selected, ec)) {
                        state->current_path = selected;
                        search_engine->set_root(selected);
                        state->search_query = ""; // Reset search
                        state->selected_file_index = 0;
                        search_engine->update_search(); // Immediate update
                        return true;
                    } else {
                        // Open it in the native OS viewer!
#ifdef _WIN32
                        std::string cmd = "start \"\" \"" + selected.string() + "\"";
#elif __APPLE__
                        std::string cmd = "open \"" + selected.string() + "\"";
#else
                        std::string cmd = "xdg-open \"" + selected.string() + "\" &";
#endif
                        std::system(cmd.c_str());
                        return true;
                    }
                }
            }
        }

        if (event == Event::ArrowRight) {
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
                    std::error_code ec;
                    if (std::filesystem::is_directory(selected, ec)) {
                        state->current_path = selected;
                        search_engine->set_root(selected);
                        state->search_query = ""; // Reset search
                        state->selected_file_index = 0;
                        search_engine->update_search(); // Immediate update
                        return true;
                    } else {
                        // It is a file: move focus to the Preview pane for scrolling!
                        state->active_pane = 2;
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
            } else if (state->active_pane == 2) {
                // Left arrow inside Preview pane: return focus to the Files list!
                state->active_pane = 1;
                return true;
            }
        }


        return false; // event not fully handled, let components have it
    });
    // Start UI
    screen.Loop(event_handler);
}
