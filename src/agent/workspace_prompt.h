#ifndef AGENT_WORKSPACE_PROMPT_H
#define AGENT_WORKSPACE_PROMPT_H

#include <filesystem>
#include <string>

/// Load a user-defined workspace prompt from bolt.md or .bolt/prompt.md.
/// Returns empty string if no prompt file is found.
std::string load_workspace_prompt(const std::filesystem::path& workspace_root);

/// Load workspace prompt from a specific file path.
std::string load_workspace_prompt_file(const std::filesystem::path& file_path);

#endif
