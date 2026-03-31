#ifndef APP_SLASH_COMMAND_REGISTRY_H
#define APP_SLASH_COMMAND_REGISTRY_H

#include <filesystem>
#include <string>
#include <vector>

enum class SlashCommandSource {
    builtin,
    workspace,
    user,
};

struct SlashCommandDirectory {
    std::filesystem::path path;
    SlashCommandSource source = SlashCommandSource::user;
};

struct SlashCommandEntry {
    std::string name;
    std::string usage;
    std::string description;
    std::string section;
    SlashCommandSource source = SlashCommandSource::builtin;
    std::filesystem::path source_path;
    std::string prompt_template;

    bool is_custom() const {
        return source != SlashCommandSource::builtin;
    }
};

class SlashCommandRegistry {
public:
    static SlashCommandRegistry with_builtin_commands();

    void add(const SlashCommandEntry& entry);
    void load_custom_commands(const std::vector<SlashCommandDirectory>& command_dirs);

    std::vector<std::string> command_names() const;
    const SlashCommandEntry* find(const std::string& name) const;
    const SlashCommandEntry* match_input(const std::string& line) const;
    std::vector<SlashCommandEntry> entries() const;

    std::string render_help() const;
    std::string expand_custom_prompt(const SlashCommandEntry& entry,
                                     const std::string& arguments) const;

private:
    std::vector<SlashCommandEntry> entries_;

    static std::string strip_heading_prefix(const std::string& content);
    static SlashCommandEntry parse_custom_command(const std::filesystem::path& file_path,
                                                  SlashCommandSource source);
};

#endif
