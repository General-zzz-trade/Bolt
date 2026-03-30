#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct Skill {
    std::string name;
    std::string description;
    std::string content;         // Full SKILL.md content
    std::filesystem::path path;
    bool auto_load = false;      // Load into every prompt
};

class SkillLoader {
public:
    // Discover skills from directory (SKILL.md files)
    static std::vector<Skill> discover(const std::filesystem::path& skills_dir);

    // Load a single skill file
    static Skill load_skill(const std::filesystem::path& skill_path);

    // Parse YAML-like frontmatter from SKILL.md
    // ---
    // name: Code Review
    // description: Guidelines for reviewing code
    // auto_load: true
    // ---
    static Skill parse_frontmatter(const std::string& content, const std::filesystem::path& path);

    // Format skills for injection into system prompt
    static std::string format_for_prompt(const std::vector<Skill>& skills);

    // Format skill list for display
    static std::string format_list(const std::vector<Skill>& skills);
};
