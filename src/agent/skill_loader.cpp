#include "skill_loader.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

std::vector<Skill> SkillLoader::discover(const std::filesystem::path& skills_dir) {
    std::vector<Skill> skills;
    if (!std::filesystem::exists(skills_dir) || !std::filesystem::is_directory(skills_dir)) {
        return skills;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(skills_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        // Case-insensitive .md check
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".md") continue;

        try {
            skills.push_back(load_skill(entry.path()));
        } catch (...) {
            // Skip files that fail to parse
        }
    }

    // Sort by name for deterministic ordering
    std::sort(skills.begin(), skills.end(),
              [](const Skill& a, const Skill& b) { return a.name < b.name; });

    return skills;
}

Skill SkillLoader::load_skill(const std::filesystem::path& skill_path) {
    std::ifstream file(skill_path);
    if (!file.is_open()) {
        Skill s;
        s.name = skill_path.stem().string();
        s.path = skill_path;
        return s;
    }
    std::ostringstream buf;
    buf << file.rdbuf();
    return parse_frontmatter(buf.str(), skill_path);
}

Skill SkillLoader::parse_frontmatter(const std::string& content, const std::filesystem::path& path) {
    Skill skill;
    skill.path = path;
    // Default name from filename stem
    skill.name = path.stem().string();

    std::string body = content;

    // Check for frontmatter delimiters
    if (content.size() >= 3 && content.substr(0, 3) == "---") {
        // Find the closing ---
        auto second = content.find("\n---", 3);
        if (second != std::string::npos) {
            // Parse frontmatter key:value pairs
            std::string frontmatter = content.substr(3, second - 3);
            std::istringstream fm_stream(frontmatter);
            std::string line;
            while (std::getline(fm_stream, line)) {
                line = trim(line);
                if (line.empty()) continue;
                auto colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string key = trim(line.substr(0, colon));
                std::string value = trim(line.substr(colon + 1));
                if (key == "name") {
                    skill.name = value;
                } else if (key == "description") {
                    skill.description = value;
                } else if (key == "auto_load") {
                    skill.auto_load = (value == "true" || value == "yes" || value == "1");
                }
            }
            // Body is everything after the closing ---
            auto body_start = second + 4; // skip "\n---"
            if (body_start < content.size()) {
                // Skip optional newline after closing ---
                if (content[body_start] == '\n') ++body_start;
                body = content.substr(body_start);
            } else {
                body = "";
            }
        }
    }

    skill.content = body;
    return skill;
}

std::string SkillLoader::format_for_prompt(const std::vector<Skill>& skills) {
    std::ostringstream out;
    bool has_auto = false;
    for (const auto& s : skills) {
        if (s.auto_load) {
            if (!has_auto) {
                out << "\n# Active Skills\n";
                has_auto = true;
            }
            out << "\n## Skill: " << s.name << "\n" << s.content << "\n";
        }
    }
    return out.str();
}

std::string SkillLoader::format_list(const std::vector<Skill>& skills) {
    if (skills.empty()) {
        return "  No skills found.\n";
    }
    std::ostringstream out;
    for (const auto& s : skills) {
        out << "  " << s.name;
        if (s.auto_load) {
            out << " [auto]";
        }
        if (!s.description.empty()) {
            out << " — " << s.description;
        }
        out << "\n";
    }
    return out.str();
}
