#include "permission_rule_engine.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

}  // namespace

std::string permission_mode_to_string(PermissionMode mode) {
    switch (mode) {
        case PermissionMode::prompt:
            return "prompt";
        case PermissionMode::auto_approve:
            return "auto-approve";
        case PermissionMode::auto_deny:
            return "auto-deny";
    }
    return "prompt";
}

bool parse_permission_mode(const std::string& value, PermissionMode* mode) {
    if (mode == nullptr) return false;
    std::string normalized = trim_copy(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (normalized == "prompt" || normalized == "default") {
        *mode = PermissionMode::prompt;
        return true;
    }
    if (normalized == "auto-approve" || normalized == "auto_approve" ||
        normalized == "auto" || normalized == "acceptedits") {
        *mode = PermissionMode::auto_approve;
        return true;
    }
    if (normalized == "auto-deny" || normalized == "auto_deny" ||
        normalized == "deny") {
        *mode = PermissionMode::auto_deny;
        return true;
    }
    return false;
}

std::string permission_scope_to_string(PermissionRuleScope scope) {
    switch (scope) {
        case PermissionRuleScope::global:
            return "global";
        case PermissionRuleScope::workspace:
            return "workspace";
    }
    return "workspace";
}

bool parse_permission_scope(const std::string& value, PermissionRuleScope* scope) {
    if (scope == nullptr) return false;
    std::string normalized = trim_copy(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (normalized == "global" || normalized == "user") {
        *scope = PermissionRuleScope::global;
        return true;
    }
    if (normalized == "workspace" || normalized == "project" || normalized == "local") {
        *scope = PermissionRuleScope::workspace;
        return true;
    }
    return false;
}

PermissionRuleEngine::PermissionRuleEngine(std::filesystem::path workspace_root)
    : global_path_(default_global_path()) {
    if (workspace_root.empty()) {
        std::error_code ec;
        workspace_root = std::filesystem::current_path(ec);
        if (ec) workspace_root.clear();
    }
    if (!workspace_root.empty()) {
        workspace_path_ = workspace_root / ".bolt" / "permissions.json";
    }
}

bool PermissionRuleEngine::load() {
    global_rules_ = load_rule_file(global_path_);
    workspace_rules_ = load_rule_file(workspace_path_);
    return true;
}

PermissionMode PermissionRuleEngine::mode() const {
    if (workspace_rules_.has_mode) return workspace_rules_.mode;
    if (global_rules_.has_mode) return global_rules_.mode;
    return PermissionMode::prompt;
}

void PermissionRuleEngine::set_mode(PermissionRuleScope scope, PermissionMode mode) {
    RuleFile* rules = mutable_scope(scope);
    if (rules == nullptr) return;
    rules->has_mode = true;
    rules->mode = mode;
    save_rule_file(scope == PermissionRuleScope::global ? global_path_ : workspace_path_, *rules);
}

bool PermissionRuleEngine::is_allowed(const std::string& tool_name) const {
    const std::string normalized = normalize_tool_name(tool_name);
    if (normalized.empty()) return false;
    if (workspace_rules_.allow_tools.count(normalized)) return true;
    if (workspace_rules_.deny_tools.count(normalized)) return false;
    if (global_rules_.allow_tools.count(normalized)) return true;
    return false;
}

bool PermissionRuleEngine::is_denied(const std::string& tool_name) const {
    const std::string normalized = normalize_tool_name(tool_name);
    if (normalized.empty()) return false;
    if (workspace_rules_.deny_tools.count(normalized)) return true;
    if (workspace_rules_.allow_tools.count(normalized)) return false;
    return global_rules_.deny_tools.count(normalized) > 0;
}

void PermissionRuleEngine::allow_tool(PermissionRuleScope scope, const std::string& tool_name) {
    RuleFile* rules = mutable_scope(scope);
    if (rules == nullptr) return;
    const std::string normalized = normalize_tool_name(tool_name);
    if (normalized.empty()) return;
    rules->deny_tools.erase(normalized);
    rules->allow_tools.insert(normalized);
    save_rule_file(scope == PermissionRuleScope::global ? global_path_ : workspace_path_, *rules);
}

void PermissionRuleEngine::deny_tool(PermissionRuleScope scope, const std::string& tool_name) {
    RuleFile* rules = mutable_scope(scope);
    if (rules == nullptr) return;
    const std::string normalized = normalize_tool_name(tool_name);
    if (normalized.empty()) return;
    rules->allow_tools.erase(normalized);
    rules->deny_tools.insert(normalized);
    save_rule_file(scope == PermissionRuleScope::global ? global_path_ : workspace_path_, *rules);
}

bool PermissionRuleEngine::remove_tool(const std::string& tool_name) {
    const std::string normalized = normalize_tool_name(tool_name);
    if (normalized.empty()) return false;
    bool removed = false;
    removed = global_rules_.allow_tools.erase(normalized) > 0 || removed;
    removed = global_rules_.deny_tools.erase(normalized) > 0 || removed;
    removed = workspace_rules_.allow_tools.erase(normalized) > 0 || removed;
    removed = workspace_rules_.deny_tools.erase(normalized) > 0 || removed;
    if (removed) {
        save_rule_file(global_path_, global_rules_);
        save_rule_file(workspace_path_, workspace_rules_);
    }
    return removed;
}

void PermissionRuleEngine::clear(PermissionRuleScope scope) {
    RuleFile* rules = mutable_scope(scope);
    if (rules == nullptr) return;
    *rules = RuleFile{};
    save_rule_file(scope == PermissionRuleScope::global ? global_path_ : workspace_path_, *rules);
}

PermissionRuleSnapshot PermissionRuleEngine::snapshot() const {
    PermissionRuleSnapshot snap;
    snap.effective_mode = mode();
    snap.global_has_mode = global_rules_.has_mode;
    snap.global_mode = global_rules_.mode;
    snap.workspace_has_mode = workspace_rules_.has_mode;
    snap.workspace_mode = workspace_rules_.mode;
    snap.global_allow_tools = sorted_tools(global_rules_.allow_tools);
    snap.global_deny_tools = sorted_tools(global_rules_.deny_tools);
    snap.workspace_allow_tools = sorted_tools(workspace_rules_.allow_tools);
    snap.workspace_deny_tools = sorted_tools(workspace_rules_.deny_tools);
    return snap;
}

const std::filesystem::path& PermissionRuleEngine::global_path() const {
    return global_path_;
}

const std::filesystem::path& PermissionRuleEngine::workspace_path() const {
    return workspace_path_;
}

std::filesystem::path PermissionRuleEngine::default_global_path() {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (home == nullptr) home = std::getenv("USERPROFILE");
#endif
    if (home == nullptr) return {};
    return std::filesystem::path(home) / ".bolt" / "permissions.json";
}

std::string PermissionRuleEngine::normalize_tool_name(const std::string& tool_name) {
    std::string normalized = trim_copy(tool_name);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

PermissionRuleEngine::RuleFile PermissionRuleEngine::load_rule_file(const std::filesystem::path& path) {
    RuleFile rules;
    if (path.empty() || !std::filesystem::exists(path)) return rules;

    try {
        std::ifstream input(path);
        if (!input) return rules;
        const json doc = json::parse(input);
        if (doc.contains("mode") && doc["mode"].is_string()) {
            PermissionMode parsed_mode;
            if (parse_permission_mode(doc["mode"].get<std::string>(), &parsed_mode)) {
                rules.has_mode = true;
                rules.mode = parsed_mode;
            }
        }
        if (doc.contains("always_allow_tools") && doc["always_allow_tools"].is_array()) {
            for (const auto& item : doc["always_allow_tools"]) {
                if (item.is_string()) {
                    const std::string normalized = normalize_tool_name(item.get<std::string>());
                    if (!normalized.empty()) rules.allow_tools.insert(normalized);
                }
            }
        }
        if (doc.contains("always_deny_tools") && doc["always_deny_tools"].is_array()) {
            for (const auto& item : doc["always_deny_tools"]) {
                if (item.is_string()) {
                    const std::string normalized = normalize_tool_name(item.get<std::string>());
                    if (!normalized.empty()) rules.deny_tools.insert(normalized);
                }
            }
        }
    } catch (...) {}
    return rules;
}

bool PermissionRuleEngine::save_rule_file(const std::filesystem::path& path, const RuleFile& rules) {
    if (path.empty()) return false;

    const bool empty = !rules.has_mode && rules.allow_tools.empty() && rules.deny_tools.empty();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (empty) {
        std::filesystem::remove(path, ec);
        return true;
    }

    try {
        json doc = json::object();
        if (rules.has_mode) {
            doc["mode"] = permission_mode_to_string(rules.mode);
        }
        doc["always_allow_tools"] = sorted_tools(rules.allow_tools);
        doc["always_deny_tools"] = sorted_tools(rules.deny_tools);

        std::ofstream output(path);
        if (!output) return false;
        output << doc.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> PermissionRuleEngine::sorted_tools(const std::set<std::string>& tools) {
    return {tools.begin(), tools.end()};
}

PermissionRuleEngine::RuleFile* PermissionRuleEngine::mutable_scope(PermissionRuleScope scope) {
    switch (scope) {
        case PermissionRuleScope::global:
            return &global_rules_;
        case PermissionRuleScope::workspace:
            return &workspace_rules_;
    }
    return nullptr;
}

const PermissionRuleEngine::RuleFile& PermissionRuleEngine::scope_rules(PermissionRuleScope scope) const {
    switch (scope) {
        case PermissionRuleScope::global:
            return global_rules_;
        case PermissionRuleScope::workspace:
            return workspace_rules_;
    }
    return workspace_rules_;
}
