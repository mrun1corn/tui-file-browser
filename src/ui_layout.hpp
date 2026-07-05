#pragma once
#include "app_state.hpp"
#include "search_engine.hpp"
#include <memory>

std::vector<std::string> get_drives();

void run_ui(std::shared_ptr<AppState> state, std::shared_ptr<SearchEngine> search_engine);
