#pragma once
#include <filesystem>
#include <string>
#include "app_config.h"

struct SetupResult {
    std::string provider;
    std::string model;
    bool completed = false;
};

// Run interactive setup wizard. Returns selected provider/model.
SetupResult run_setup_wizard();

// Check if setup has been completed (config file exists at ~/.bolt/config.json)
bool is_setup_complete();

// Save setup result to ~/.bolt/config.json
bool save_setup_config(const SetupResult& result);

// Load saved setup config into AppConfig. Returns false if no config saved.
bool load_setup_config(AppConfig& config);

// Get the path to the global config file (~/.bolt/config.json)
std::filesystem::path get_global_config_path();

// Interactive model selection (for /model command) - returns new provider:model or empty if cancelled
SetupResult run_model_selector(const std::string& current_provider, const std::string& current_model);
