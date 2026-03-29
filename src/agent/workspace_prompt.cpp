#include "workspace_prompt.h"

#include <fstream>
#include <sstream>

std::string load_workspace_prompt(const std::filesystem::path& workspace_root) {
    // Try bolt.md first, then .bolt/prompt.md
    const std::filesystem::path candidates[] = {
        workspace_root / "bolt.md",
        workspace_root / ".bolt" / "prompt.md",
    };

    for (const auto& path : candidates) {
        std::string content = load_workspace_prompt_file(path);
        if (!content.empty()) {
            return content;
        }
    }
    return "";
}

std::string load_workspace_prompt_file(const std::filesystem::path& file_path) {
    if (!std::filesystem::exists(file_path)) {
        return "";
    }
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}
