#include "settings_store.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

}  // namespace

SettingsStore::SettingsStore(std::filesystem::path workspace_root)
    : workspace_root_(std::move(workspace_root)),
      user_path_(default_user_settings_path()) {
    if (!workspace_root_.empty()) {
        project_path_ = workspace_root_ / ".bolt" / "settings.json";
        local_path_ = workspace_root_ / ".bolt" / "settings.local.json";
    }
}

bool SettingsStore::load() {
    user_settings_ = load_json_file(user_path_);
    project_settings_ = load_json_file(project_path_);
    local_settings_ = load_json_file(local_path_);
    return true;
}

nlohmann::json SettingsStore::resolved() const {
    nlohmann::json result = nlohmann::json::object();
    merge_json(&result, user_settings_);
    merge_json(&result, project_settings_);
    merge_json(&result, local_settings_);
    return result;
}

nlohmann::json SettingsStore::get(const std::string& dotted_path) const {
    const nlohmann::json all_settings = resolved();
    const auto path = split_dotted_path(dotted_path);
    const nlohmann::json* found = lookup(all_settings, path);
    return found == nullptr ? nlohmann::json() : *found;
}

bool SettingsStore::contains(const std::string& dotted_path) const {
    const nlohmann::json all_settings = resolved();
    return lookup(all_settings, split_dotted_path(dotted_path)) != nullptr;
}

bool SettingsStore::set(SettingsScope scope, const std::string& dotted_path,
                        const nlohmann::json& value) {
    nlohmann::json* target =
        get_scope_json(scope, &user_settings_, &project_settings_, &local_settings_);
    if (target == nullptr) return false;

    const auto path = split_dotted_path(dotted_path);
    nlohmann::json* slot = lookup_mutable(target, path, true);
    if (slot == nullptr) return false;
    *slot = value;

    switch (scope) {
        case SettingsScope::user:
            return save_json_file(user_path_, user_settings_);
        case SettingsScope::project:
            return save_json_file(project_path_, project_settings_);
        case SettingsScope::local:
            return save_json_file(local_path_, local_settings_);
    }
    return false;
}

bool SettingsStore::erase(SettingsScope scope, const std::string& dotted_path) {
    nlohmann::json* target =
        get_scope_json(scope, &user_settings_, &project_settings_, &local_settings_);
    if (target == nullptr) return false;

    const auto path = split_dotted_path(dotted_path);
    if (path.empty()) return false;

    nlohmann::json* parent = target;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        if (!parent->is_object()) return false;
        auto it = parent->find(path[i]);
        if (it == parent->end()) return false;
        parent = &(*it);
    }
    if (!parent->is_object()) return false;
    if (parent->erase(path.back()) == 0) return false;

    switch (scope) {
        case SettingsScope::user:
            return save_json_file(user_path_, user_settings_);
        case SettingsScope::project:
            return save_json_file(project_path_, project_settings_);
        case SettingsScope::local:
            return save_json_file(local_path_, local_settings_);
    }
    return false;
}

std::vector<std::filesystem::path> SettingsStore::additional_command_dirs() const {
    std::vector<std::filesystem::path> dirs;
    const nlohmann::json value = get("commands.additional_dirs");
    if (value.is_array()) {
        for (const auto& item : value) {
            if (item.is_string() && !item.get<std::string>().empty()) {
                dirs.emplace_back(item.get<std::string>());
            }
        }
    } else if (value.is_string()) {
        dirs.emplace_back(value.get<std::string>());
    }
    return dirs;
}

std::string SettingsStore::format_resolved() const {
    const nlohmann::json all_settings = resolved();
    if (!all_settings.is_object() || all_settings.empty()) {
        return "  (no settings)\n";
    }

    std::vector<std::string> lines;
    collect_flattened(all_settings, "", &lines);
    std::sort(lines.begin(), lines.end());

    std::ostringstream output;
    for (const auto& line : lines) {
        output << "  " << line << "\n";
    }
    return output.str();
}

nlohmann::json SettingsStore::parse_value_literal(const std::string& raw_value) {
    const std::string trimmed = trim_copy(raw_value);
    if (trimmed.empty()) return "";

    try {
        return nlohmann::json::parse(trimmed);
    } catch (...) {
        return trimmed;
    }
}

std::string SettingsStore::scope_to_string(SettingsScope scope) {
    switch (scope) {
        case SettingsScope::user:
            return "user";
        case SettingsScope::project:
            return "project";
        case SettingsScope::local:
            return "local";
    }
    return "unknown";
}

bool SettingsStore::parse_scope(const std::string& value, SettingsScope* scope) {
    if (scope == nullptr) return false;
    if (value == "user" || value == "global") {
        *scope = SettingsScope::user;
        return true;
    }
    if (value == "project" || value == "workspace") {
        *scope = SettingsScope::project;
        return true;
    }
    if (value == "local") {
        *scope = SettingsScope::local;
        return true;
    }
    return false;
}

const std::filesystem::path& SettingsStore::user_path() const {
    return user_path_;
}

const std::filesystem::path& SettingsStore::project_path() const {
    return project_path_;
}

const std::filesystem::path& SettingsStore::local_path() const {
    return local_path_;
}

std::filesystem::path SettingsStore::default_user_settings_path() {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (home == nullptr) home = std::getenv("USERPROFILE");
#endif
    if (home == nullptr) return {};
    return std::filesystem::path(home) / ".bolt" / "settings.json";
}

nlohmann::json SettingsStore::load_json_file(const std::filesystem::path& path) {
    if (path.empty() || !std::filesystem::exists(path)) {
        return nlohmann::json::object();
    }

    try {
        std::ifstream input(path);
        if (!input) return nlohmann::json::object();
        const nlohmann::json value = nlohmann::json::parse(input);
        return value.is_object() ? value : nlohmann::json::object();
    } catch (...) {
        return nlohmann::json::object();
    }
}

bool SettingsStore::save_json_file(const std::filesystem::path& path, const nlohmann::json& value) {
    if (path.empty()) return false;
    try {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream output(path);
        if (!output) return false;
        output << value.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

void SettingsStore::merge_json(nlohmann::json* target, const nlohmann::json& overlay) {
    if (target == nullptr || !overlay.is_object()) return;
    if (!target->is_object()) {
        *target = nlohmann::json::object();
    }

    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        if (it->is_object() && (*target)[it.key()].is_object()) {
            merge_json(&(*target)[it.key()], *it);
        } else {
            (*target)[it.key()] = *it;
        }
    }
}

nlohmann::json* SettingsStore::get_scope_json(SettingsScope scope,
                                              nlohmann::json* user_settings,
                                              nlohmann::json* project_settings,
                                              nlohmann::json* local_settings) {
    switch (scope) {
        case SettingsScope::user:
            return user_settings;
        case SettingsScope::project:
            return project_settings;
        case SettingsScope::local:
            return local_settings;
    }
    return nullptr;
}

std::vector<std::string> SettingsStore::split_dotted_path(const std::string& dotted_path) {
    std::vector<std::string> parts;
    std::stringstream input(dotted_path);
    std::string part;
    while (std::getline(input, part, '.')) {
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

const nlohmann::json* SettingsStore::lookup(const nlohmann::json& root,
                                            const std::vector<std::string>& path) {
    const nlohmann::json* current = &root;
    for (const auto& part : path) {
        if (!current->is_object()) return nullptr;
        auto it = current->find(part);
        if (it == current->end()) return nullptr;
        current = &(*it);
    }
    return current;
}

nlohmann::json* SettingsStore::lookup_mutable(nlohmann::json* root,
                                              const std::vector<std::string>& path,
                                              bool create_missing) {
    if (root == nullptr) return nullptr;
    nlohmann::json* current = root;
    for (const auto& part : path) {
        if (!current->is_object()) {
            if (!create_missing) return nullptr;
            *current = nlohmann::json::object();
        }
        if (!current->contains(part)) {
            if (!create_missing) return nullptr;
            (*current)[part] = nlohmann::json::object();
        }
        current = &(*current)[part];
    }
    return current;
}

void SettingsStore::collect_flattened(const nlohmann::json& value, const std::string& prefix,
                                      std::vector<std::string>* lines) {
    if (lines == nullptr) return;
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            const std::string next_prefix = prefix.empty() ? it.key() : prefix + "." + it.key();
            collect_flattened(*it, next_prefix, lines);
        }
        return;
    }

    if (value.is_string()) {
        lines->push_back(prefix + " = \"" + value.get<std::string>() + "\"");
        return;
    }
    lines->push_back(prefix + " = " + value.dump());
}
