#include "logger.hpp"
#include "app_state.hpp"
#include "search_engine.hpp"
#include "ui_layout.hpp"
#include <memory>

int main() {
    Logger::init("tui_browser.log");
    LOG("========================================");
    LOG("App started");

    auto state = std::make_shared<AppState>();
    auto engine = std::make_shared<SearchEngine>(state);
    auto initial_drives = get_drives();
    if (!initial_drives.empty()) {
        engine->set_root(initial_drives[0]);
    }

    run_ui(state, engine);
    
    return 0;
}
