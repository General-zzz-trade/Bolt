#ifndef APP_PERMISSION_RULE_ENGINE_H
#define APP_PERMISSION_RULE_ENGINE_H

#include <filesystem>
#include <set>
#include <string>
#include <vector>

enum class PermissionMode {
    prompt,
    auto_approve,
    auto_deny,
};

enum class PermissionRuleScope {
    global,
    workspace,
};

struct PermissionRuleSnapshot {
    PermissionMode effective_mode = PermissionMode::prompt;
    bool global_has_mode = false;
    PermissionMode global_mode = PermissionMode::prompt;
    bool workspace_has_mode = false;
    PermissionMode workspace_mode = PermissionMode::prompt;
    std::vector<std::string> global_allow_tools;
    std::vector<std::string> global_deny_tools;
    std::vector<std::string> workspace_allow_tools;
    std::vector<std::string> workspace_deny_tools;
};

std::string permission_mode_to_string(PermissionMode mode);
bool parse_permission_mode(const std::string& value, PermissionMode* mode);
std::string permission_scope_to_string(PermissionRuleScope scope);
bool parse_permission_scope(const std::string& value, PermissionRuleScope* scope);

class PermissionRuleEngine {
public:
    explicit PermissionRuleEngine(std::filesystem::path workspace_root = {});

    bool load();

    PermissionMode mode() const;
    void set_mode(PermissionRuleScope scope, PermissionMode mode);

    bool is_allowed(const std::string& tool_name) const;
    bool is_denied(const std::string& tool_name) const;

    void allow_tool(PermissionRuleScope scope, const std::string& tool_name);
    void deny_tool(PermissionRuleScope scope, const std::string& tool_name);
    bool remove_tool(const std::string& tool_name);
    void clear(PermissionRuleScope scope);

    PermissionRuleSnapshot snapshot() const;

    const std::filesystem::path& global_path() const;
    const std::filesystem::path& workspace_path() const;

private:
    struct RuleFile {
        bool has_mode = false;
        PermissionMode mode = PermissionMode::prompt;
        std::set<std::string> allow_tools;
        std::set<std::string> deny_tools;
    };

    std::filesystem::path global_path_;
    std::filesystem::path workspace_path_;
    RuleFile global_rules_;
    RuleFile workspace_rules_;

    static std::filesystem::path default_global_path();
    static std::string normalize_tool_name(const std::string& tool_name);
    static RuleFile load_rule_file(const std::filesystem::path& path);
    static bool save_rule_file(const std::filesystem::path& path, const RuleFile& rules);
    static std::vector<std::string> sorted_tools(const std::set<std::string>& tools);
    RuleFile* mutable_scope(PermissionRuleScope scope);
    const RuleFile& scope_rules(PermissionRuleScope scope) const;
};

#endif
