#ifndef APP_SETTINGS_STORE_H
#define APP_SETTINGS_STORE_H

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

enum class SettingsScope {
    user,
    project,
    local,
};

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path workspace_root = {});

    bool load();

    nlohmann::json resolved() const;
    nlohmann::json get(const std::string& dotted_path) const;
    bool contains(const std::string& dotted_path) const;

    bool set(SettingsScope scope, const std::string& dotted_path, const nlohmann::json& value);
    bool erase(SettingsScope scope, const std::string& dotted_path);

    std::vector<std::filesystem::path> additional_command_dirs() const;
    std::string format_resolved() const;

    static nlohmann::json parse_value_literal(const std::string& raw_value);
    static std::string scope_to_string(SettingsScope scope);
    static bool parse_scope(const std::string& value, SettingsScope* scope);

    const std::filesystem::path& user_path() const;
    const std::filesystem::path& project_path() const;
    const std::filesystem::path& local_path() const;

private:
    std::filesystem::path workspace_root_;
    std::filesystem::path user_path_;
    std::filesystem::path project_path_;
    std::filesystem::path local_path_;

    nlohmann::json user_settings_;
    nlohmann::json project_settings_;
    nlohmann::json local_settings_;

    static std::filesystem::path default_user_settings_path();
    static nlohmann::json load_json_file(const std::filesystem::path& path);
    static bool save_json_file(const std::filesystem::path& path, const nlohmann::json& value);
    static void merge_json(nlohmann::json* target, const nlohmann::json& overlay);
    static nlohmann::json* get_scope_json(SettingsScope scope,
                                          nlohmann::json* user_settings,
                                          nlohmann::json* project_settings,
                                          nlohmann::json* local_settings);
    static std::vector<std::string> split_dotted_path(const std::string& dotted_path);
    static const nlohmann::json* lookup(const nlohmann::json& root,
                                        const std::vector<std::string>& path);
    static nlohmann::json* lookup_mutable(nlohmann::json* root,
                                          const std::vector<std::string>& path,
                                          bool create_missing);
    static void collect_flattened(const nlohmann::json& value, const std::string& prefix,
                                  std::vector<std::string>* lines);
};

#endif
