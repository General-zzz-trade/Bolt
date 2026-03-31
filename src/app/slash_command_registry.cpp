#include "slash_command_registry.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string replace_all(std::string text, const std::string& needle, const std::string& replacement) {
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

std::vector<SlashCommandEntry> builtin_entries() {
    return {
        {"/help", "/help", "Show interactive command reference", "Session"},
        {"/save", "/save [name]", "Save current session", "Session"},
        {"/load", "/load <id>", "Load a saved session", "Session"},
        {"/sessions", "/sessions", "List saved sessions", "Session"},
        {"/delete", "/delete <id>", "Delete a saved session", "Session"},
        {"/export", "/export [file]", "Export transcript to markdown", "Session"},
        {"/rename", "/rename <name>", "Rename current session", "Session"},
        {"/whoami", "/whoami", "Show session information", "Session"},
        {"/id", "/id", "Alias for /whoami", "Session"},
        {"/clear", "/clear", "Clear conversation history", "Context"},
        {"/compact", "/compact", "Compress context to save tokens", "Context"},
        {"/context", "/context", "Show context window usage", "Context"},
        {"/undo", "/undo", "Revert the last file edit", "Context"},
        {"/reset", "/reset", "Reset history, undo stack, and session id", "Context"},
        {"/btw", "/btw <question>", "Ask a side question without polluting the main thread", "Context"},
        {"/model", "/model [name]", "Show or switch model", "Display"},
        {"/cost", "/cost", "Show token usage and cost", "Display"},
        {"/status", "/status", "Show current status", "Display"},
        {"/tools", "/tools [verbose]", "List available tools", "Display"},
        {"/diff", "/diff", "Show git diff", "Display"},
        {"/doctor", "/doctor", "Run environment diagnostics", "Display"},
        {"/debug", "/debug", "Toggle debug output", "Display"},
        {"/fast", "/fast", "Toggle fast mode", "Mode"},
        {"/think", "/think [normal|deep|fast]", "Set reasoning depth", "Mode"},
        {"/verbose", "/verbose", "Toggle verbose/debug output", "Mode"},
        {"/plan", "/plan", "Explain plan-mode behavior", "Mode"},
        {"/auto", "/auto", "Explain auto-approve mode", "Mode"},
        {"/permissions", "/permissions [subcommand]", "Inspect or persist approval rules", "Mode"},
        {"/config", "/config [subcommand]", "View or update layered CLI settings", "Mode"},
        {"/init", "/init", "Create bolt.md project config", "System"},
        {"/stop", "/stop", "Stop current operation", "System"},
        {"/plugins", "/plugins", "List installed plugins", "System"},
        {"/skills", "/skills [load <name>]", "List and load skills", "System"},
        {"/sandbox", "/sandbox", "Show sandbox status", "System"},
        {"/team", "/team <tasks>", "Run parallel tasks on worktrees", "System"},
        {"/memory", "/memory [cmd]", "Manage persistent memory", "System"},
        {"/quit", "/quit", "Exit Bolt", "System"},
        {"/exit", "/exit", "Alias for /quit", "System"},
        {"/q", "/q", "Alias for /quit", "System"},
    };
}

}  // namespace

SlashCommandRegistry SlashCommandRegistry::with_builtin_commands() {
    SlashCommandRegistry registry;
    for (const auto& entry : builtin_entries()) {
        registry.add(entry);
    }
    return registry;
}

void SlashCommandRegistry::add(const SlashCommandEntry& entry) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const SlashCommandEntry& current) { return current.name == entry.name; });
    if (it == entries_.end()) {
        entries_.push_back(entry);
    } else {
        *it = entry;
    }
}

void SlashCommandRegistry::load_custom_commands(const std::vector<SlashCommandDirectory>& command_dirs) {
    for (const auto& dir_entry : command_dirs) {
        const auto& dir = dir_entry.path;
        if (dir.empty() || !std::filesystem::exists(dir)) continue;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".md") continue;
            add(parse_custom_command(entry.path(), dir_entry.source));
        }
    }
}

std::vector<std::string> SlashCommandRegistry::command_names() const {
    std::vector<std::string> names;
    for (const auto& entry : entries_) {
        names.push_back(entry.name);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

const SlashCommandEntry* SlashCommandRegistry::find(const std::string& name) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const SlashCommandEntry& entry) { return entry.name == name; });
    return it == entries_.end() ? nullptr : &(*it);
}

const SlashCommandEntry* SlashCommandRegistry::match_input(const std::string& line) const {
    for (const auto& entry : entries_) {
        if (!entry.is_custom()) continue;
        if (line == entry.name) return &entry;
        if (line.rfind(entry.name + " ", 0) == 0) return &entry;
    }
    return nullptr;
}

std::vector<SlashCommandEntry> SlashCommandRegistry::entries() const {
    return entries_;
}

std::string SlashCommandRegistry::render_help() const {
    std::vector<SlashCommandEntry> sorted = entries_;
    std::sort(sorted.begin(), sorted.end(),
              [](const SlashCommandEntry& a, const SlashCommandEntry& b) {
                  if (a.section != b.section) return a.section < b.section;
                  return a.name < b.name;
              });

    std::ostringstream output;
    std::string current_section;
    for (const auto& entry : sorted) {
        if (entry.section != current_section) {
            current_section = entry.section;
            output << "\n\033[1;35m " << current_section << "\033[0m\n";
        }
        output << "  \033[1m" << entry.usage << "\033[0m";
        const std::size_t visible = entry.usage.size();
        if (visible < 22) {
            output << std::string(22 - visible, ' ');
        } else {
            output << " ";
        }
        output << entry.description;
        if (entry.is_custom() && !entry.source_path.empty()) {
            output << " \033[2m(" << entry.source_path.filename().string() << ")\033[0m";
        }
        output << "\n";
    }
    return output.str();
}

std::string SlashCommandRegistry::expand_custom_prompt(const SlashCommandEntry& entry,
                                                       const std::string& arguments) const {
    std::string prompt = entry.prompt_template;
    if (prompt.find("{{args}}") != std::string::npos) {
        return replace_all(prompt, "{{args}}", arguments);
    }
    if (arguments.empty()) return prompt;
    if (!prompt.empty() && prompt.back() != '\n') {
        prompt += "\n\n";
    }
    prompt += "User arguments:\n" + arguments;
    return prompt;
}

std::string SlashCommandRegistry::strip_heading_prefix(const std::string& content) {
    std::stringstream input(content);
    std::string line;
    bool skipped_heading = false;
    std::ostringstream output;
    while (std::getline(input, line)) {
        if (!skipped_heading) {
            const std::string trimmed = trim_copy(line);
            if (trimmed.empty()) continue;
            if (!trimmed.empty() && trimmed[0] == '#') {
                skipped_heading = true;
                continue;
            }
            skipped_heading = true;
        }
        output << line << "\n";
    }
    return trim_copy(output.str());
}

SlashCommandEntry SlashCommandRegistry::parse_custom_command(const std::filesystem::path& file_path,
                                                             SlashCommandSource source) {
    SlashCommandEntry entry;
    entry.name = "/" + file_path.stem().string();
    entry.usage = entry.name + " [args]";
    entry.section = "Custom";
    entry.source = source;
    entry.source_path = file_path;

    std::ifstream input(file_path);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    entry.prompt_template = strip_heading_prefix(content);

    std::stringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        entry.description = trimmed;
        break;
    }
    if (entry.description.empty()) {
        entry.description = "Run custom workflow from " + file_path.filename().string();
    }

    return entry;
}
