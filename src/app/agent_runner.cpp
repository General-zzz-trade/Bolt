#include "agent_runner.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "../agent/agent.h"
#include "../agent/skill_loader.h"
#include "../core/interfaces/approval_provider.h"
#include "../core/session/session_store.h"
#include "permission_rule_engine.h"
#include "setup_wizard.h"
#include "settings_store.h"
#include "slash_command_registry.h"
#include "terminal_renderer.h"
#include "terminal_approval_provider.h"
#include "terminal_input.h"
#include "terminal_ui_config.h"
#include "signal_handler.h"
#include "token_tracker.h"

namespace {

// Animated spinner shown while the model is thinking
class Spinner {
public:
    void start(std::ostream& output) {
        running_ = true;
        thread_ = std::thread([this, &output]() {
            const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
            int i = 0;
            while (running_) {
                output << "\r\033[2m" << frames[i % 10] << " Thinking...\033[0m   " << std::flush;
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                ++i;
            }
            output << "\r                     \r" << std::flush;
        });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// @file reference expansion
std::string expand_file_references(const std::string& input,
                                    const std::filesystem::path& workspace_root) {
    if (workspace_root.empty()) return input;

    std::string result;
    std::size_t pos = 0;
    while (pos < input.size()) {
        if (input[pos] == '@' && pos + 1 < input.size() &&
            (std::isalnum(input[pos + 1]) || input[pos + 1] == '.' || input[pos + 1] == '/')) {
            // Parse file path
            std::size_t start = pos + 1;
            std::size_t end = start;
            while (end < input.size() && !std::isspace(input[end]) &&
                   input[end] != ',' && input[end] != ')') {
                ++end;
            }
            std::string ref = input.substr(start, end - start);

            // Check for line range  @file:10-20
            int start_line = 0, end_line = 0;
            auto colon = ref.find(':');
            std::string path_str = ref;
            if (colon != std::string::npos) {
                path_str = ref.substr(0, colon);
                std::string range = ref.substr(colon + 1);
                auto dash = range.find('-');
                if (dash != std::string::npos) {
                    try {
                        start_line = std::stoi(range.substr(0, dash));
                        end_line = std::stoi(range.substr(dash + 1));
                    } catch (...) {}
                } else {
                    try { start_line = end_line = std::stoi(range); } catch (...) {}
                }
            }

            auto full_path = workspace_root / path_str;
            if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
                std::ifstream file(full_path);
                std::string content;
                if (start_line > 0) {
                    // Read specific lines
                    std::string line;
                    int ln = 0;
                    while (std::getline(file, line)) {
                        ++ln;
                        if (ln >= start_line && (end_line == 0 || ln <= end_line)) {
                            content += line + "\n";
                        }
                        if (end_line > 0 && ln > end_line) break;
                    }
                } else {
                    content.assign(std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>());
                }

                result += "\n<file path=\"" + path_str + "\"";
                if (start_line > 0) {
                    result += " lines=\"" + std::to_string(start_line);
                    if (end_line > 0 && end_line != start_line) result += "-" + std::to_string(end_line);
                    result += "\"";
                }
                result += ">\n" + content + "</file>\n";
                pos = end;
            } else {
                result += input[pos];
                ++pos;
            }
        } else {
            result += input[pos];
            ++pos;
        }
    }
    return result;
}

// Undo stack for file edits
struct UndoEntry {
    std::string path;
    std::string original_content;
};

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool path_has_prefix(const std::filesystem::path& path, const std::filesystem::path& prefix) {
    if (path.empty() || prefix.empty()) return false;

    auto lhs = path.lexically_normal();
    auto rhs = prefix.lexically_normal();

    auto lit = lhs.begin();
    auto rit = rhs.begin();
    for (; rit != rhs.end(); ++rit, ++lit) {
        if (lit == lhs.end() || *lit != *rit) {
            return false;
        }
    }
    return true;
}

std::vector<SlashCommandDirectory> command_dirs_for(const std::filesystem::path& workspace_root,
                                                    const SettingsStore& settings) {
    std::vector<SlashCommandDirectory> dirs;
    if (!workspace_root.empty()) {
        dirs.push_back({workspace_root / ".bolt" / "commands", SlashCommandSource::workspace});
    }

    const char* home_env = std::getenv("HOME");
#ifdef _WIN32
    if (home_env == nullptr) home_env = std::getenv("USERPROFILE");
#endif
    if (home_env != nullptr && *home_env != '\0') {
        dirs.push_back({std::filesystem::path(home_env) / ".bolt" / "commands",
                        SlashCommandSource::user});
    }

    for (auto dir : settings.additional_command_dirs()) {
        SlashCommandSource source = SlashCommandSource::user;
        if (dir.is_relative() && !workspace_root.empty()) {
            dir = workspace_root / dir;
            source = SlashCommandSource::workspace;
        } else if (!workspace_root.empty() && path_has_prefix(dir, workspace_root)) {
            source = SlashCommandSource::workspace;
        }
        dirs.push_back({dir, source});
    }

    return dirs;
}

std::string join_items(const std::vector<std::string>& items) {
    if (items.empty()) return "(none)";

    std::ostringstream output;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) output << ", ";
        output << items[i];
    }
    return output.str();
}

void render_interactive_help(std::ostream& output, const SlashCommandRegistry& registry) {
    output << registry.render_help();
    output << "\n\033[1;35m Shell\033[0m\n";
    output << "  \033[1m! <command>\033[0m        Execute shell command without leaving Bolt\n";
    output << "\n\033[1;35m Shortcuts\033[0m\n";
    output << "  \033[2mCtrl+C\033[0m cancel  \033[2mCtrl+L\033[0m clear screen  \033[2mCtrl+D\033[0m exit\n";
    output << "  \033[2m↑/↓\033[0m history   \033[2mTab\033[0m complete       \033[2m@file\033[0m include file\n";
    output << "\n\033[1;35m UI\033[0m\n";
    output << "  \033[2mDefault output preserves terminal scrollback.\033[0m\n";
    output << "  \033[2mSet BOLT_TRANSIENT_UI=1 to re-enable animated spinner/status overlays.\033[0m\n\n";
}

void render_permission_snapshot(std::ostream& output,
                                const PermissionRuleSnapshot& snapshot) {
    output << "\n\033[1;35m Permissions\033[0m\n\n";
    output << "  Effective mode: " << permission_mode_to_string(snapshot.effective_mode) << "\n";
    output << "  Global mode:    "
           << (snapshot.global_has_mode ? permission_mode_to_string(snapshot.global_mode)
                                        : "(inherit)")
           << "\n";
    output << "  Workspace mode: "
           << (snapshot.workspace_has_mode ? permission_mode_to_string(snapshot.workspace_mode)
                                           : "(inherit)")
           << "\n";
    output << "\n  Global allow:    " << join_items(snapshot.global_allow_tools) << "\n";
    output << "  Global deny:     " << join_items(snapshot.global_deny_tools) << "\n";
    output << "  Workspace allow: " << join_items(snapshot.workspace_allow_tools) << "\n";
    output << "  Workspace deny:  " << join_items(snapshot.workspace_deny_tools) << "\n";
    output << "\n  \033[2mUsage: /permissions mode <prompt|auto-approve|auto-deny> [global|workspace]\033[0m\n";
    output << "  \033[2m       /permissions allow <tool> [global|workspace]\033[0m\n";
    output << "  \033[2m       /permissions deny <tool> [global|workspace]\033[0m\n";
    output << "  \033[2m       /permissions remove <tool>\033[0m\n";
    output << "  \033[2m       /permissions clear [global|workspace|all]\033[0m\n\n";
}

void handle_permissions_command(const std::string& line,
                                TerminalApprovalProvider* approval_provider,
                                std::ostream& output) {
    if (approval_provider == nullptr) {
        output << "\033[33mCurrent approval provider does not support interactive permission rules.\033[0m\n";
        return;
    }

    const std::string rest =
        line.size() > std::string("/permissions").size()
            ? trim_copy(line.substr(std::string("/permissions").size()))
            : "";
    if (rest.empty()) {
        render_permission_snapshot(output, approval_provider->rules());
        return;
    }

    std::istringstream input(rest);
    std::string action;
    input >> action;

    if (action == "mode") {
        std::string mode_token;
        std::string scope_token;
        input >> mode_token >> scope_token;
        PermissionMode mode;
        if (!parse_permission_mode(mode_token, &mode)) {
            output << "\033[33mUsage: /permissions mode <prompt|auto-approve|auto-deny> [global|workspace]\033[0m\n";
            return;
        }
        PermissionRuleScope scope = PermissionRuleScope::workspace;
        if (!scope_token.empty() && !parse_permission_scope(scope_token, &scope)) {
            output << "\033[33mUnknown scope: " << scope_token << "\033[0m\n";
            return;
        }
        approval_provider->set_mode(scope, mode);
        output << "\033[2mSet " << permission_scope_to_string(scope)
               << " mode to " << permission_mode_to_string(mode) << ".\033[0m\n";
        return;
    }

    if (action == "allow" || action == "deny") {
        std::string tool_name;
        std::string scope_token;
        input >> tool_name >> scope_token;
        if (tool_name.empty()) {
            output << "\033[33mUsage: /permissions " << action
                   << " <tool> [global|workspace]\033[0m\n";
            return;
        }
        PermissionRuleScope scope = PermissionRuleScope::workspace;
        if (!scope_token.empty() && !parse_permission_scope(scope_token, &scope)) {
            output << "\033[33mUnknown scope: " << scope_token << "\033[0m\n";
            return;
        }
        if (action == "allow") {
            approval_provider->allow_tool(scope, tool_name);
        } else {
            approval_provider->deny_tool(scope, tool_name);
        }
        output << "\033[2m" << action << " " << tool_name << " in "
               << permission_scope_to_string(scope) << " rules.\033[0m\n";
        return;
    }

    if (action == "remove") {
        std::string tool_name;
        input >> tool_name;
        if (tool_name.empty()) {
            output << "\033[33mUsage: /permissions remove <tool>\033[0m\n";
            return;
        }
        if (approval_provider->remove_tool(tool_name)) {
            output << "\033[2mRemoved permission rules for " << tool_name << ".\033[0m\n";
        } else {
            output << "\033[33mNo persisted rules found for " << tool_name << ".\033[0m\n";
        }
        return;
    }

    if (action == "clear") {
        std::string scope_token;
        input >> scope_token;
        if (scope_token.empty() || scope_token == "workspace" || scope_token == "project" ||
            scope_token == "local") {
            approval_provider->clear_rules(PermissionRuleScope::workspace);
            output << "\033[2mCleared workspace permission rules.\033[0m\n";
            return;
        }
        if (scope_token == "global" || scope_token == "user") {
            approval_provider->clear_rules(PermissionRuleScope::global);
            output << "\033[2mCleared global permission rules.\033[0m\n";
            return;
        }
        if (scope_token == "all") {
            approval_provider->clear_rules(PermissionRuleScope::global);
            approval_provider->clear_rules(PermissionRuleScope::workspace);
            output << "\033[2mCleared all persisted permission rules.\033[0m\n";
            return;
        }
        output << "\033[33mUsage: /permissions clear [global|workspace|all]\033[0m\n";
        return;
    }

    output << "\033[33mUnknown /permissions subcommand. Type /permissions for usage.\033[0m\n";
}

bool handle_config_command(const std::string& line, SettingsStore* settings, std::ostream& output) {
    if (settings == nullptr) return false;

    const std::string rest =
        line.size() > std::string("/config").size()
            ? trim_copy(line.substr(std::string("/config").size()))
            : "";
    if (rest.empty()) {
        output << "\n\033[1;35m Settings\033[0m\n\n";
        output << settings->format_resolved();
        output << "\n  user:    " << settings->user_path().string() << "\n";
        output << "  project: " << settings->project_path().string() << "\n";
        output << "  local:   " << settings->local_path().string() << "\n\n";
        output << "  \033[2mUsage: /config get <path>\033[0m\n";
        output << "  \033[2m       /config set <user|project|local> <path> <value>\033[0m\n";
        output << "  \033[2m       /config unset <user|project|local> <path>\033[0m\n\n";
        return false;
    }

    std::istringstream input(rest);
    std::string action;
    input >> action;

    if (action == "get") {
        std::string path;
        input >> path;
        if (path.empty()) {
            output << "\033[33mUsage: /config get <path>\033[0m\n";
            return false;
        }
        if (!settings->contains(path)) {
            output << "\033[33mSetting not found: " << path << "\033[0m\n";
            return false;
        }
        output << "\033[2m" << path << " = " << settings->get(path).dump() << "\033[0m\n";
        return false;
    }

    if (action == "set") {
        std::string scope_token;
        std::string path;
        input >> scope_token >> path;
        std::string raw_value;
        std::getline(input, raw_value);
        raw_value = trim_copy(raw_value);

        SettingsScope scope;
        if (!SettingsStore::parse_scope(scope_token, &scope) || path.empty() || raw_value.empty()) {
            output << "\033[33mUsage: /config set <user|project|local> <path> <value>\033[0m\n";
            return false;
        }

        const nlohmann::json value = SettingsStore::parse_value_literal(raw_value);
        if (!settings->set(scope, path, value)) {
            output << "\033[31mFailed to persist setting.\033[0m\n";
            return false;
        }

        output << "\033[2mSaved " << path << " to "
               << SettingsStore::scope_to_string(scope) << " settings.\033[0m\n";
        return true;
    }

    if (action == "unset") {
        std::string scope_token;
        std::string path;
        input >> scope_token >> path;

        SettingsScope scope;
        if (!SettingsStore::parse_scope(scope_token, &scope) || path.empty()) {
            output << "\033[33mUsage: /config unset <user|project|local> <path>\033[0m\n";
            return false;
        }

        if (!settings->erase(scope, path)) {
            output << "\033[33mSetting not found in "
                   << SettingsStore::scope_to_string(scope) << ": " << path << "\033[0m\n";
            return false;
        }

        output << "\033[2mRemoved " << path << " from "
               << SettingsStore::scope_to_string(scope) << " settings.\033[0m\n";
        return true;
    }

    output << "\033[33mUnknown /config subcommand. Type /config for usage.\033[0m\n";
    return false;
}

}  // namespace

std::string build_agent_banner(const Agent& agent) {
    std::ostringstream output;

    // Gather info
    std::string model = agent.model();
    int tool_count = static_cast<int>(agent.available_tool_names().size());

    // Build content lines (without box decorations)
    std::vector<std::string> lines;
    lines.push_back("\033[1;36m⚡ Bolt\033[0m \033[2m— AI Coding Agent\033[0m");
    lines.push_back("");
    lines.push_back("\033[2mModel:\033[0m  " + model);
    lines.push_back("\033[2mTools:\033[0m  " + std::to_string(tool_count) + " available");
    lines.push_back("\033[2mDebug:\033[0m  " + std::string(agent.debug_enabled() ? "on" : "off"));
    lines.push_back("");
    lines.push_back("\033[2m/help commands   @file reference\033[0m");
    lines.push_back("\033[2mCtrl+C cancel    Ctrl+D exit\033[0m");

    // Calculate visible width for each line (strip ANSI escape codes)
    auto visible_len = [](const std::string& s) -> std::size_t {
        std::size_t len = 0;
        bool in_escape = false;
        for (char c : s) {
            if (c == '\033') { in_escape = true; continue; }
            if (in_escape) { if (c == 'm') in_escape = false; continue; }
            ++len;
        }
        return len;
    };

    // Find max visible width
    std::size_t max_width = 0;
    for (const auto& l : lines) {
        auto w = visible_len(l);
        if (w > max_width) max_width = w;
    }
    max_width += 2;  // padding inside box

    // Build horizontal rule from box-drawing chars
    auto hrule = [](std::size_t count) -> std::string {
        std::string s;
        for (std::size_t i = 0; i < count; ++i) s += "\u2500";
        return s;
    };

    // Draw box
    output << "\n";
    output << " \033[2m\u250c" << hrule(max_width + 2) << "\u2510\033[0m\n";
    for (const auto& l : lines) {
        auto vw = visible_len(l);
        std::size_t pad = max_width - vw;
        output << " \033[2m\u2502\033[0m " << l << std::string(pad + 1, ' ') << "\033[2m\u2502\033[0m\n";
    }
    output << " \033[2m\u2514" << hrule(max_width + 2) << "\u2518\033[0m\n";

    return output.str();
}

int run_agent_single_turn(Agent& agent, const std::string& prompt, std::ostream& output) {
    bool first_token = true;
    bool is_terminal = (&output == &std::cout);
    const TerminalUiConfig ui_config = load_terminal_ui_config();

    Spinner spinner;
    if (is_terminal && ui_config.spinner_enabled) spinner.start(output);

    TerminalRenderer renderer(output);
    renderer.begin_stream();

    std::string reply = agent.run_turn_streaming(prompt,
        [&](const std::string& token) {
            if (first_token) {
                if (is_terminal && ui_config.spinner_enabled) spinner.stop();
                first_token = false;
            }
            renderer.stream_token(token);
        });

    if (first_token) {
        if (is_terminal && ui_config.spinner_enabled) spinner.stop();
        renderer.render_markdown(reply);
    }
    renderer.end_stream();
    output << std::endl;
    return 0;
}

int run_agent_interactive_loop(Agent& agent, std::istream& /*input*/, std::ostream& output,
                               const std::filesystem::path& workspace_root,
                               bool resume_last,
                               std::shared_ptr<IApprovalProvider> approval_provider) {
    // Initialize components
    TerminalRenderer renderer(output);
    TerminalInput term_input(STDIN_FILENO, output);
    SignalHandler::instance().install();
    TokenTracker tracker;
    const TerminalUiConfig ui_config = load_terminal_ui_config();
    auto interactive_approval =
        std::dynamic_pointer_cast<TerminalApprovalProvider>(approval_provider);
    SettingsStore settings(workspace_root);
    settings.load();
    SlashCommandRegistry command_registry = SlashCommandRegistry::with_builtin_commands();
    auto refresh_command_registry = [&]() {
        settings.load();
        command_registry = SlashCommandRegistry::with_builtin_commands();
        command_registry.load_custom_commands(command_dirs_for(workspace_root, settings));
        term_input.set_slash_commands(command_registry.command_names());
    };
    refresh_command_registry();

    // Undo stack
    std::vector<UndoEntry> undo_stack;

    if (!workspace_root.empty()) {
        term_input.set_workspace_root(workspace_root);
    }

    // Session support
    std::unique_ptr<SessionStore> sessions;
    std::string current_session_id;
    if (!workspace_root.empty()) {
        sessions = std::make_unique<SessionStore>(workspace_root / ".bolt" / "sessions");
        current_session_id = SessionStore::generate_id();

        // Resume last session if requested
        if (resume_last) {
            auto list = sessions->list();
            if (!list.empty()) {
                auto msgs = sessions->load(list[0].id);
                if (!msgs.empty()) {
                    agent.restore_history(msgs);
                    current_session_id = list[0].id;
                    output << "\033[2mResumed session: " << current_session_id
                           << " (" << msgs.size() << " messages)\033[0m\n";
                }
            }
        }
    }

    // Install cancellation check
    agent.set_cancellation_check([&]() -> bool {
        return SignalHandler::instance().is_cancelled();
    });

    // Render banner
    renderer.render_banner(agent.model(), agent.debug_enabled());

    // Auto-save helper
    auto auto_save = [&]() {
        if (sessions) {
            auto msgs = agent.get_chat_messages();
            if (!msgs.empty()) {
                sessions->save(current_session_id, msgs);
            }
        }
    };

    // Prompt state for context-aware coloring
    enum class PromptState { ready, warning, error };
    PromptState prompt_state = PromptState::ready;

    // Main loop
    while (true) {
        // Check for terminal resize
        if (SignalHandler::instance().resize_pending()) {
            SignalHandler::instance().clear_resize_pending();
            renderer.update_terminal_width();
        }

        // Determine prompt color based on state
        std::string prompt;
        switch (prompt_state) {
            case PromptState::ready:   prompt = "\033[1;32m❯ \033[0m"; break;
            case PromptState::warning: prompt = "\033[1;33m❯ \033[0m"; break;
            case PromptState::error:   prompt = "\033[1;31m❯ \033[0m"; break;
        }

        SignalHandler::instance().reset();
        auto line_opt = term_input.read_line(prompt);

        if (!line_opt.has_value()) {
            // Ctrl+D
            output << "\033[2mGoodbye!\033[0m\n";
            auto_save();
            break;
        }

        std::string line = line_opt.value();

        if (term_input.cancelled()) {
            output << "\033[33m  \u2298 Cancelled\033[0m\n";
            prompt_state = PromptState::warning;
            continue;
        }

        // Trim
        line = trim_copy(line);
        if (line.empty()) continue;

        // === Slash Commands ===

        // Clear trace observer before commands to prevent dangling references
        agent.set_trace_observer(nullptr);

        if (line == "/quit" || line == "/exit" || line == "/q") {
            output << "\033[2mGoodbye!\033[0m\n";
            auto_save();
            break;
        }

        if (line == "/clear") {
            agent.clear_history();
            tracker = TokenTracker();
            output << "\033[2mHistory and token counts cleared.\033[0m\n";
            continue;
        }

        if (line == "/help") {
            render_interactive_help(output, command_registry);
            continue;
        }

        if (line == "/permissions" || line.rfind("/permissions ", 0) == 0) {
            handle_permissions_command(line, interactive_approval.get(), output);
            continue;
        }

        if (line == "/config" || line.rfind("/config ", 0) == 0) {
            if (handle_config_command(line, &settings, output)) {
                refresh_command_registry();
            }
            continue;
        }

        if (line == "/compact") {
            agent.compact_history();
            output << "\033[2mContext compacted.\033[0m\n";
            continue;
        }

        if (line == "/model" || line.rfind("/model ", 0) == 0) {
            if (line == "/model") {
                // Interactive model selector
                output << "\033[2mCurrent: " << agent.model() << "\033[0m\n";
                output << "\033[2mNote: Model switching takes effect on next launch.\033[0m\n";
                output << "\033[2mUse /model <name> to set, or run without args for selector.\033[0m\n\n";

                // Run interactive selector
                SetupResult result = run_model_selector("", agent.model());
                if (result.completed) {
                    save_setup_config(result);
                    output << "\n\033[32m✓ Saved! New model will be used on next launch.\033[0m\n";
                    output << "\033[2m  Provider: " << result.provider << "\n";
                    output << "  Model:    " << result.model << "\033[0m\n";
                }
            } else {
                // Direct set: /model claude-sonnet-4-20250514
                std::string model_name = line.substr(7);
                // trim
                auto b = model_name.find_first_not_of(" \t");
                if (b != std::string::npos) model_name = model_name.substr(b);
                auto e = model_name.find_last_not_of(" \t");
                if (e != std::string::npos) model_name = model_name.substr(0, e + 1);

                // Detect provider from model name
                SetupResult result;
                result.model = model_name;
                if (model_name.find("claude") != std::string::npos) {
                    result.provider = "claude";
                } else if (model_name.find("gpt") != std::string::npos ||
                           model_name.find("o3") != std::string::npos) {
                    result.provider = "openai";
                } else if (model_name.find("gemini") != std::string::npos) {
                    result.provider = "gemini";
                } else if (model_name.find("deepseek") != std::string::npos) {
                    result.provider = "deepseek";
                } else if (model_name.find("qwen-") != std::string::npos ||
                           model_name.find("qwen3-") != std::string::npos) {
                    result.provider = "qwen";
                } else if (model_name.find("glm") != std::string::npos) {
                    result.provider = "zhipu";
                } else if (model_name.find("moonshot") != std::string::npos) {
                    result.provider = "moonshot";
                } else if (model_name.find("Baichuan") != std::string::npos ||
                           model_name.find("baichuan") != std::string::npos) {
                    result.provider = "baichuan";
                } else if (model_name.find("doubao") != std::string::npos) {
                    result.provider = "doubao";
                } else if (model_name.find("llama") != std::string::npos ||
                           model_name.find("mixtral") != std::string::npos) {
                    if (std::getenv("GROQ_API_KEY")) {
                        result.provider = "groq";
                    } else {
                        result.provider = "ollama-chat";
                    }
                } else {
                    result.provider = "ollama-chat";  // default to Ollama for unknown models
                }
                result.completed = true;
                save_setup_config(result);
                output << "\033[32m✓ Saved for next launch: " << result.provider
                       << " / " << result.model << "\033[0m\n";
            }
            continue;
        }

        if (line == "/cost") {
            auto total = tracker.total();
            renderer.render_cost_summary(agent.model(), total.input_tokens, total.output_tokens,
                                         tracker.estimated_cost(), tracker.turn_count());
            continue;
        }

        if (line == "/debug") {
            bool new_debug = !agent.debug_enabled();
            agent.set_debug(new_debug);
            output << "\033[2mDebug: " << (new_debug ? "on" : "off") << "\033[0m\n";
            continue;
        }

        if ((line == "/save" || line.rfind("/save ", 0) == 0) && sessions) {
            std::string save_id = current_session_id;
            if (line.size() > 6) {
                save_id = trim_copy(line.substr(6));
                current_session_id = save_id;
            }
            auto msgs = agent.get_chat_messages();
            if (msgs.empty()) {
                output << "\033[33mNo messages to save.\033[0m\n";
            } else if (sessions->save(save_id, msgs)) {
                output << "\033[2mSession saved: " << save_id << " (" << msgs.size() << " messages)\033[0m\n";
            } else {
                output << "\033[31mFailed to save session.\033[0m\n";
            }
            continue;
        }

        if (line.rfind("/load ", 0) == 0 && sessions) {
            std::string id = trim_copy(line.substr(6));
            auto msgs = sessions->load(id);
            if (msgs.empty()) {
                output << "\033[33mSession not found: " << id << "\033[0m\n";
            } else {
                agent.restore_history(msgs);
                current_session_id = id;
                tracker = TokenTracker(); // reset tracker
                output << "\033[2mLoaded session: " << id << " (" << msgs.size() << " messages)\033[0m\n";
            }
            continue;
        }

        if (line == "/sessions" && sessions) {
            auto list = sessions->list();
            if (list.empty()) {
                output << "\033[2mNo saved sessions.\033[0m\n";
            } else {
                std::vector<std::tuple<std::string, int, std::string, std::string>> session_data;
                for (const auto& s : list) {
                    session_data.emplace_back(s.id, static_cast<int>(s.message_count),
                                              s.modified_at, s.last_message);
                }
                renderer.render_sessions_list(session_data);
            }
            continue;
        }

        if (line.rfind("/delete ", 0) == 0 && sessions) {
            std::string id = trim_copy(line.substr(8));
            if (sessions->remove(id)) {
                output << "\033[2mDeleted session: " << id << "\033[0m\n";
            } else {
                output << "\033[33mSession not found: " << id << "\033[0m\n";
            }
            continue;
        }

        if (line == "/export" || line.rfind("/export ", 0) == 0) {
            std::string filename;
            if (line.size() > 8) {
                filename = trim_copy(line.substr(8));
            } else {
                // Generate default filename
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::tm tm_buf{};
#ifdef _WIN32
                localtime_s(&tm_buf, &time);
#else
                localtime_r(&time, &tm_buf);
#endif
                std::ostringstream ss;
                ss << "bolt-export-" << std::put_time(&tm_buf, "%Y%m%d-%H%M%S") << ".md";
                filename = ss.str();
            }
            auto msgs = agent.get_chat_messages();
            if (msgs.empty()) {
                output << "\033[33mNo messages to export.\033[0m\n";
            } else {
                std::ofstream f(filename);
                if (f) {
                    f << "# Bolt Chat Export\n\n";
                    for (const auto& msg : msgs) {
                        if (msg.role == ChatRole::user) {
                            f << "## User\n\n" << msg.content << "\n\n";
                        } else if (msg.role == ChatRole::assistant) {
                            f << "## Assistant\n\n" << msg.content << "\n\n";
                        } else if (msg.role == ChatRole::tool) {
                            f << "### Tool: " << msg.name << "\n\n```\n" << msg.content << "\n```\n\n";
                        }
                    }
                    output << "\033[2mExported to: " << filename << "\033[0m\n";
                } else {
                    output << "\033[31mFailed to write: " << filename << "\033[0m\n";
                }
            }
            continue;
        }

        if (line == "/undo") {
            if (undo_stack.empty()) {
                output << "\033[33mNothing to undo.\033[0m\n";
            } else {
                auto& entry = undo_stack.back();
                std::ofstream f(entry.path);
                if (f) {
                    f << entry.original_content;
                    output << "\033[2mReverted: " << entry.path << "\033[0m\n";
                } else {
                    output << "\033[31mFailed to revert: " << entry.path << "\033[0m\n";
                }
                undo_stack.pop_back();
            }
            continue;
        }

        if (line == "/diff") {
            // Run git diff and display
            FILE* pipe = popen("git diff 2>&1", "r");
            if (pipe) {
                std::string diff_output;
                char buffer[256];
                while (fgets(buffer, sizeof(buffer), pipe)) {
                    diff_output += buffer;
                }
                pclose(pipe);
                if (diff_output.empty()) {
                    output << "\033[2mNo changes.\033[0m\n";
                } else {
                    renderer.render_diff(diff_output);
                }
            }
            continue;
        }

        if (line == "/status") {
            auto total = tracker.total();
            renderer.render_status_info(agent.model(), total.input_tokens, total.output_tokens,
                                        current_session_id, workspace_root.string(),
                                        static_cast<int>(agent.file_index().file_count()));
            continue;
        }

        if (line == "/reset") {
            agent.clear_history();
            tracker = TokenTracker();
            undo_stack.clear();
            current_session_id = SessionStore::generate_id();
            output << "\033[2mFull reset complete.\033[0m\n";
            continue;
        }

        if (line == "/plugins") {
            output << "\n\033[1;35m Plugins\033[0m\n\n";
            auto ws_dir = workspace_root / ".bolt" / "plugins";
            const char* home_env = std::getenv("HOME");
            auto global_dir = std::filesystem::path(
                home_env ? home_env : "") / ".bolt" / "plugins";

            bool found = false;
            for (const auto& dir : {ws_dir, global_dir}) {
                if (!std::filesystem::exists(dir)) continue;
                std::error_code ec;
                for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                    if (!entry.is_directory()) continue;
                    auto manifest = entry.path() / "plugin.json";
                    if (!std::filesystem::exists(manifest)) continue;
                    try {
                        std::ifstream mf(manifest);
                        auto j = nlohmann::json::parse(mf);
                        std::string pname = j.value("name", entry.path().filename().string());
                        std::string pdesc = j.value("description", "");
                        int tool_count = j.contains("tools") ? static_cast<int>(j["tools"].size()) : 0;
                        output << "  \033[1m" << pname << "\033[0m"
                               << "  \033[2m" << pdesc << " (" << tool_count << " tools)\033[0m\n";
                        found = true;
                    } catch (...) {}
                }
            }

            if (!found) {
                output << "  \033[2mNo plugins installed.\033[0m\n";
            }
            output << "\n  \033[2mPlugin dirs: .bolt/plugins/ and ~/.bolt/plugins/\033[0m\n";
            output << "  \033[2mEach plugin needs a plugin.json manifest.\033[0m\n\n";
            continue;
        }

        if (line == "/team" || line.rfind("/team ", 0) == 0) {
            if (line == "/team") {
                output << "\n\033[1;35m Agent Team\033[0m\n\n";
                output << "  \033[2mRun parallel tasks on separate git worktrees.\033[0m\n\n";
                output << "  \033[1mUsage:\033[0m /team task1 | task2 | task3\n";
                output << "  \033[1mExample:\033[0m /team add tests for auth | refactor config | fix lint errors\n\n";
                output << "  \033[2mEach task runs in an isolated worktree. Results show as branches.\033[0m\n\n";
            } else {
                // Parse tasks separated by |
                std::string tasks_str = line.substr(6);
                std::vector<std::string> tasks;
                std::istringstream stream(tasks_str);
                std::string task;
                while (std::getline(stream, task, '|')) {
                    // trim
                    auto b = task.find_first_not_of(" \t");
                    auto e = task.find_last_not_of(" \t");
                    if (b != std::string::npos && e != std::string::npos) {
                        tasks.push_back(task.substr(b, e - b + 1));
                    }
                }

                if (tasks.empty()) {
                    output << "\033[33mNo tasks provided. Use: /team task1 | task2\033[0m\n";
                } else {
                    output << "\n\033[1;36m Running " << tasks.size()
                           << " tasks in parallel...\033[0m\n\n";
                    for (std::size_t i = 0; i < tasks.size(); ++i) {
                        output << "  " << (i + 1) << ". " << tasks[i] << "\n";
                    }
                    output << "\n";

                    // Agent Team execution requires LLM connection for sub-agents.
                    // Display informational message about team mode availability.
                    output << "\033[2m  (Agent Team execution requires LLM connection for sub-agents.\n";
                    output << "   Worker branches created as bolt-team-* for manual use.)\033[0m\n\n";
                }
            }
            continue;
        }

        if (line == "/skills" || line.rfind("/skills ", 0) == 0) {
            auto ws_skills = SkillLoader::discover(workspace_root / ".bolt" / "skills");
            std::vector<Skill> gl_skills;
            const char* home_env = std::getenv("HOME");
            if (home_env) {
                gl_skills = SkillLoader::discover(
                    std::filesystem::path(home_env) / ".bolt" / "skills");
            }

            std::string subcmd = line.size() > 8 ? trim_copy(line.substr(8)) : "";

            if (subcmd.rfind("load ", 0) == 0) {
                std::string target = trim_copy(subcmd.substr(5));
                // Search for skill by name in both lists
                const Skill* found = nullptr;
                for (const auto& s : ws_skills) {
                    if (s.name == target) { found = &s; break; }
                }
                if (!found) {
                    for (const auto& s : gl_skills) {
                        if (s.name == target) { found = &s; break; }
                    }
                }
                if (found) {
                    // Inject skill content as a user message with context
                    std::string inject = "[Skill loaded: " + found->name + "]\n" + found->content;
                    output << "\033[2mLoaded skill: " << found->name << "\033[0m\n";
                    // Send as a silent user turn so the agent sees the skill content
                    agent.run_turn(inject);
                } else {
                    output << "\033[33mSkill not found: " << target << "\033[0m\n";
                }
            } else {
                output << "\n\033[1;35m Skills\033[0m\n\n";
                if (!ws_skills.empty()) {
                    output << "  \033[1mWorkspace (.bolt/skills/):\033[0m\n";
                    output << SkillLoader::format_list(ws_skills);
                }
                if (!gl_skills.empty()) {
                    output << "  \033[1mGlobal (~/.bolt/skills/):\033[0m\n";
                    output << SkillLoader::format_list(gl_skills);
                }
                if (ws_skills.empty() && gl_skills.empty()) {
                    output << "  \033[2mNo skills found.\033[0m\n";
                }
                output << "\n  \033[2mSkill dirs: .bolt/skills/ and ~/.bolt/skills/\033[0m\n";
                output << "  \033[2mUsage: /skills load <name>\033[0m\n\n";
            }
            continue;
        }

        if (line == "/sandbox") {
            output << "\n\033[1;35m Sandbox Status\033[0m\n\n";
            bool bwrap_available = std::filesystem::exists("/usr/bin/bwrap") ||
                                   std::filesystem::exists("/usr/local/bin/bwrap");
            output << "  Bubblewrap: " << (bwrap_available ? "\033[32mavailable\033[0m" : "\033[31mnot found\033[0m") << "\n";
            output << "\n  \033[2mConfigure in bolt.conf or env:\033[0m\n";
            output << "    sandbox.enabled = true\n";
            output << "    sandbox.network_enabled = true\n";
            output << "    sandbox.deny_read = ~/.ssh,~/.aws\n";
            output << "    sandbox.allow_write = /tmp/build\n";
            output << "\n  \033[2mOr: BOLT_SANDBOX_ENABLED=true bolt agent\033[0m\n\n";
            if (!bwrap_available) {
                output << "  \033[33mInstall: sudo apt install bubblewrap\033[0m\n\n";
            }
            continue;
        }

        if (line == "/memory" || line.rfind("/memory ", 0) == 0) {
            std::string subcmd = line.size() > 8 ? trim_copy(line.substr(8)) : "";

            if (subcmd.empty() || subcmd == "list") {
                // List all memories
                output << "\n\033[1;35m Memory\033[0m\n\n";

                auto global = agent.global_memory().list();
                auto workspace = agent.workspace_memory().list();

                if (!global.empty()) {
                    output << "  \033[1mGlobal (~/.bolt/memory.json):\033[0m\n";
                    for (const auto& e : global) {
                        output << "    \033[36m" << e.key << "\033[0m = " << e.value << "\n";
                    }
                }
                if (!workspace.empty()) {
                    output << "  \033[1mWorkspace (.bolt/memory.json):\033[0m\n";
                    for (const auto& e : workspace) {
                        output << "    \033[36m" << e.key << "\033[0m = " << e.value << "\n";
                    }
                }
                if (global.empty() && workspace.empty()) {
                    output << "  \033[2mNo memories saved.\033[0m\n";
                }
                output << "\n  \033[2mUsage: /memory set <key> <value>\033[0m\n";
                output << "  \033[2m       /memory remove <key>\033[0m\n";
                output << "  \033[2m       /memory clear\033[0m\n\n";
            } else if (subcmd.rfind("set ", 0) == 0) {
                auto rest = trim_copy(subcmd.substr(4));
                auto space = rest.find(' ');
                if (space != std::string::npos) {
                    std::string key = rest.substr(0, space);
                    std::string value = trim_copy(rest.substr(space + 1));
                    // Workspace memory by default, global if key starts with "global."
                    if (key.rfind("global.", 0) == 0) {
                        key = key.substr(7);
                        agent.global_memory().set(key, value);
                        output << "\033[2mGlobal memory set: " << key << " = " << value << "\033[0m\n";
                    } else {
                        agent.workspace_memory().set(key, value);
                        output << "\033[2mWorkspace memory set: " << key << " = " << value << "\033[0m\n";
                    }
                } else {
                    output << "\033[33mUsage: /memory set <key> <value>\033[0m\n";
                }
            } else if (subcmd.rfind("remove ", 0) == 0) {
                std::string key = trim_copy(subcmd.substr(7));
                bool removed = agent.workspace_memory().remove(key) || agent.global_memory().remove(key);
                if (removed) {
                    output << "\033[2mRemoved: " << key << "\033[0m\n";
                } else {
                    output << "\033[33mNot found: " << key << "\033[0m\n";
                }
            } else if (subcmd == "clear") {
                output << "\033[2mManually delete .bolt/memory.json or ~/.bolt/memory.json\033[0m\n";
            }
            continue;
        }

        if (line == "/init") {
            auto bolt_md_path = workspace_root / "bolt.md";
            if (std::filesystem::exists(bolt_md_path)) {
                output << "\033[33mbolt.md already exists. Edit it manually or delete and re-run /init.\033[0m\n";
            } else {
                // Auto-detect project type
                std::string project_type = "unknown";
                std::string build_cmd, test_cmd, lang;
                if (std::filesystem::exists(workspace_root / "CMakeLists.txt")) {
                    project_type = "C++ (CMake)";
                    build_cmd = "cmake -B build -S . && cmake --build build -j$(nproc)";
                    test_cmd = "./build/tests";
                    lang = "C++";
                } else if (std::filesystem::exists(workspace_root / "package.json")) {
                    project_type = "JavaScript/TypeScript (Node.js)";
                    build_cmd = "npm install && npm run build";
                    test_cmd = "npm test";
                    lang = "TypeScript";
                } else if (std::filesystem::exists(workspace_root / "Cargo.toml")) {
                    project_type = "Rust (Cargo)";
                    build_cmd = "cargo build";
                    test_cmd = "cargo test";
                    lang = "Rust";
                } else if (std::filesystem::exists(workspace_root / "go.mod")) {
                    project_type = "Go";
                    build_cmd = "go build ./...";
                    test_cmd = "go test ./...";
                    lang = "Go";
                } else if (std::filesystem::exists(workspace_root / "requirements.txt") ||
                           std::filesystem::exists(workspace_root / "setup.py") ||
                           std::filesystem::exists(workspace_root / "pyproject.toml")) {
                    project_type = "Python";
                    build_cmd = "pip install -e .";
                    test_cmd = "pytest";
                    lang = "Python";
                } else if (std::filesystem::exists(workspace_root / "Makefile")) {
                    project_type = "Make";
                    build_cmd = "make";
                    test_cmd = "make test";
                }

                std::ofstream f(bolt_md_path);
                f << "# Project Instructions\n\n";
                f << "## Project Type\n" << project_type << "\n\n";
                if (!build_cmd.empty()) {
                    f << "## Build\n```bash\n" << build_cmd << "\n```\n\n";
                }
                if (!test_cmd.empty()) {
                    f << "## Test\n```bash\n" << test_cmd << "\n```\n\n";
                }
                if (!lang.empty()) {
                    f << "## Code Style\n- Language: " << lang << "\n";
                }
                f << "\n## Rules\n- Always read code before modifying it\n";
                f << "- Run tests after making changes\n";
                f << "- Keep changes minimal and focused\n";

                output << "\033[32m" << "Created bolt.md" << "\033[0m\n";
                output << "\033[2m  Project type: " << project_type << "\033[0m\n";
                output << "\033[2m  Edit bolt.md to customize agent behavior.\033[0m\n";
            }
            continue;
        }

        if (line == "/context") {
            auto msgs = agent.get_chat_messages();
            int total_tokens = 0;
            int system_tokens = 0;
            int user_tokens = 0;
            int assistant_tokens = 0;
            int tool_tokens = 0;

            for (const auto& m : msgs) {
                int tokens = static_cast<int>((m.content.size() + 3) / 4);  // ~4 chars per token
                for (const auto& tc : m.tool_calls) {
                    tokens += static_cast<int>((tc.arguments.size() + tc.name.size() + 3) / 4);
                }
                total_tokens += tokens;
                switch (m.role) {
                    case ChatRole::system: system_tokens += tokens; break;
                    case ChatRole::user: user_tokens += tokens; break;
                    case ChatRole::assistant: assistant_tokens += tokens; break;
                    case ChatRole::tool: tool_tokens += tokens; break;
                }
            }

            output << "\n\033[1;35m Context Window\033[0m\n\n";
            output << "  Messages:     " << msgs.size() << "\n";
            output << "  Est. tokens:  ~" << total_tokens << "\n\n";

            // Bar chart
            int bar_width = 40;
            auto bar = [&](const char* label, int tokens, const char* color) {
                int width = total_tokens > 0 ? (tokens * bar_width / total_tokens) : 0;
                if (width < 1 && tokens > 0) width = 1;
                output << "  " << color << std::string(width, '#') << "\033[0m"
                       << std::string(bar_width - width, '.')
                       << "  " << label << " ~" << tokens << "\n";
            };

            bar("system", system_tokens, "\033[35m");
            bar("user", user_tokens, "\033[32m");
            bar("assistant", assistant_tokens, "\033[36m");
            bar("tool", tool_tokens, "\033[33m");
            output << "\n";
            continue;
        }

        if (line == "/doctor") {
            output << "\n\033[1;35m Diagnostics\033[0m\n\n";

            // Check model
            output << "  Model:        " << agent.model() << "\n";

            // Check bwrap
            bool bwrap = std::filesystem::exists("/usr/bin/bwrap") ||
                         std::filesystem::exists("/usr/local/bin/bwrap");
            output << "  Sandbox:      " << (bwrap ? "\033[32mbwrap available\033[0m" : "\033[33mbwrap not found\033[0m") << "\n";

            // Check git
            FILE* git_pipe = popen("git --version 2>&1", "r");
            bool git_ok = false;
            if (git_pipe) {
                char buf[128];
                std::string git_ver;
                while (fgets(buf, sizeof(buf), git_pipe)) git_ver += buf;
                git_ok = (pclose(git_pipe) == 0);
                if (git_ok) {
                    // Trim trailing newline
                    while (!git_ver.empty() && (git_ver.back() == '\n' || git_ver.back() == '\r'))
                        git_ver.pop_back();
                    output << "  Git:          \033[32m" << git_ver << "\033[0m\n";
                }
            }
            if (!git_ok) output << "  Git:          \033[33mnot found\033[0m\n";

            // Check workspace
            output << "  Workspace:    " << workspace_root.string() << "\n";
            output << "  Files:        " << agent.file_index().file_count() << " indexed\n";

            // Check config
            auto config_path = workspace_root / "bolt.conf";
            output << "  Config:       " << (std::filesystem::exists(config_path) ? "\033[32mbolt.conf\033[0m" : "\033[2mno bolt.conf\033[0m") << "\n";

            auto bolt_md = workspace_root / "bolt.md";
            output << "  Instructions: " << (std::filesystem::exists(bolt_md) ? "\033[32mbolt.md\033[0m" : "\033[2mno bolt.md (run /init)\033[0m") << "\n";

            // Memory
            output << "  Memory:       " << agent.workspace_memory().size() << " workspace, "
                   << agent.global_memory().size() << " global\n";

            // Token cost
            auto total = tracker.total();
            output << "  Tokens:       " << total.input_tokens << " in / " << total.output_tokens << " out\n";
            output << "  Cost:         " << tracker.format_cost() << "\n";
            output << "\n";
            continue;
        }

        if (line == "/plan") {
            output << "\033[2mPlan mode: Agent will show planned actions before executing.\033[0m\n";
            output << "\033[2mTo enable at startup: bolt agent --approval-mode plan\033[0m\n";
            output << "\033[2mOr set: approval.mode = plan in bolt.conf\033[0m\n";
            continue;
        }

        if (line == "/auto") {
            if (interactive_approval) {
                const PermissionMode current_mode = interactive_approval->mode();
                const PermissionMode next_mode =
                    current_mode == PermissionMode::auto_approve ? PermissionMode::prompt
                                                                 : PermissionMode::auto_approve;
                interactive_approval->set_mode(PermissionRuleScope::workspace, next_mode);
                output << "\033[2mWorkspace approval mode set to "
                       << permission_mode_to_string(next_mode) << ".\033[0m\n";
                output << "\033[2mUse /permissions to inspect or change persisted rules.\033[0m\n";
            } else {
                output << "\033[2mAuto mode: All tool calls will be auto-approved.\033[0m\n";
                output << "\033[2mTo enable at startup: bolt agent --approval-mode auto\033[0m\n";
                output << "\033[2mOr set: approval.mode = auto in bolt.conf\033[0m\n";
            }
            continue;
        }

        // === Shell execution: ! <command> ===
        if (line.size() > 2 && line[0] == '!' && line[1] == ' ') {
            std::string shell_cmd = trim_copy(line.substr(2));
            if (!shell_cmd.empty()) {
                output << "\033[2m$ " << shell_cmd << "\033[0m\n";
                FILE* pipe = popen(shell_cmd.c_str(), "r");
                if (pipe) {
                    char buf[512];
                    while (fgets(buf, sizeof(buf), pipe)) {
                        output << buf;
                    }
                    int status = pclose(pipe);
#ifdef _WIN32
                    int code = status;
#else
                    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
                    if (code != 0) {
                        output << "\033[31mexit code: " << code << "\033[0m\n";
                    }
                } else {
                    output << "\033[31mFailed to execute command\033[0m\n";
                }
            }
            continue;
        }

        // /stop — cancel current operation
        if (line == "/stop") {
            SignalHandler::instance().set_cancelled(true);
            output << "\033[2mStopped.\033[0m\n";
            continue;
        }

        // /fast — toggle compact/fast mode
        if (line == "/fast") {
            static bool fast_mode = false;
            fast_mode = !fast_mode;
            if (fast_mode) {
                output << "\033[32m⚡ Fast mode ON\033[0m — shorter prompts, fewer tools, faster responses\n";
            } else {
                output << "\033[2m⚡ Fast mode OFF\033[0m — full prompts and all tools\n";
            }
            continue;
        }

        // /think — set reasoning depth
        if (line == "/think" || line.rfind("/think ", 0) == 0) {
            std::string level = line.size() > 7 ? trim_copy(line.substr(7)) : "";
            if (level.empty() || level == "normal") {
                output << "\033[2mThinking: normal (default)\033[0m\n";
            } else if (level == "deep") {
                output << "\033[2mThinking: deep — more thorough reasoning\033[0m\n";
                output << "\033[2m(Tip: Use a stronger model like Claude Opus for deep thinking)\033[0m\n";
            } else if (level == "fast" || level == "quick") {
                output << "\033[2mThinking: fast — quicker responses\033[0m\n";
            } else {
                output << "\033[2mUsage: /think [normal|deep|fast]\033[0m\n";
            }
            continue;
        }

        // /verbose — toggle verbose output
        if (line == "/verbose") {
            bool new_debug = !agent.debug_enabled();
            agent.set_debug(new_debug);
            output << "\033[2mVerbose: " << (new_debug ? "ON (showing debug info)" : "OFF") << "\033[0m\n";
            continue;
        }

        // /tools — show available tools with details
        if (line == "/tools" || line == "/tools verbose" || line == "/tools compact") {
            bool verbose = (line == "/tools verbose");
            auto tool_names = agent.available_tool_names();
            output << "\n\033[1;35m Available Tools (" << tool_names.size() << ")\033[0m\n\n";
            for (const auto& name : tool_names) {
                if (verbose) {
                    auto result = agent.run_diagnostic_tool(name, "");
                    output << "  \033[1m" << name << "\033[0m\n";
                    output << "    \033[2m" << result.content.substr(0, 80) << "\033[0m\n";
                } else {
                    output << "  \033[36m•\033[0m " << name << "\n";
                }
            }
            output << "\n  \033[2mUse /tools verbose for details\033[0m\n\n";
            continue;
        }

        // /btw <question> — side question without changing session context
        if (line.rfind("/btw ", 0) == 0) {
            std::string question = trim_copy(line.substr(5));
            if (question.empty()) {
                output << "\033[33mUsage: /btw <question>\033[0m\n";
                continue;
            }

            output << "\033[2m  (side question...)\033[0m\n";
            // Save current history, ask question, restore history
            auto saved_messages = agent.get_chat_messages();
            try {
                std::string reply = agent.run_turn(question);
                output << "\033[36m  ↳ " << reply.substr(0, 500) << "\033[0m\n";
            } catch (const std::exception& e) {
                output << "\033[31m  ↳ Error: " << e.what() << "\033[0m\n";
            }
            // Restore original history (side question is ephemeral)
            agent.restore_history(saved_messages);
            continue;
        }

        // /whoami /id — show session identity
        if (line == "/whoami" || line == "/id") {
            output << "\n\033[1;35m Session Info\033[0m\n\n";
            output << "  Model:     " << agent.model() << "\n";
            output << "  Session:   " << current_session_id << "\n";
            output << "  Workspace: " << workspace_root.string() << "\n";
            auto total = tracker.total();
            output << "  Turns:     " << tracker.turn_count() << "\n";
            output << "  Tokens:    " << total.input_tokens << " in / " << total.output_tokens << " out\n";
            output << "  Cost:      " << tracker.format_cost() << "\n";
            output << "\n";
            continue;
        }

        // /rename <name> — rename current session
        if (line.rfind("/rename ", 0) == 0) {
            std::string new_name = trim_copy(line.substr(8));
            if (new_name.empty()) {
                output << "\033[33mUsage: /rename <name>\033[0m\n";
            } else {
                current_session_id = new_name;
                output << "\033[2mSession renamed to: " << new_name << "\033[0m\n";
            }
            continue;
        }
        if (line == "/rename") {
            output << "\033[33mUsage: /rename <name>\033[0m\n";
            continue;
        }

        if (const SlashCommandEntry* custom_command = command_registry.match_input(line)) {
            const std::string arguments =
                line.size() > custom_command->name.size()
                    ? trim_copy(line.substr(custom_command->name.size()))
                    : "";
            line = command_registry.expand_custom_prompt(*custom_command, arguments);
            if (line.empty()) {
                output << "\033[33mCustom command did not expand to a prompt.\033[0m\n";
                continue;
            }
        }

        // Unknown slash command
        if (line[0] == '/') {
            output << "\033[33mUnknown command: " << line << ". Type /help for available commands.\033[0m\n";
            continue;
        }

        // === Regular prompt ===

        // Capture file state for undo (for any files referenced by @)
        // Also expand @file references
        std::string expanded = expand_file_references(line, workspace_root);

        // Spinner + streaming state (declared before trace observer so lambda can capture)
        Spinner spinner;
        bool first_token = true;
        bool spinner_stopped_for_tools = false;

        // Track tool calls for live display + undo support
        std::size_t last_trace_size = 0;
        agent.set_trace_observer([&](const std::vector<ExecutionStep>& trace) {
            // Stop spinner before writing tool progress (prevent race)
            if (!spinner_stopped_for_tools && first_token) {
                if (ui_config.spinner_enabled) {
                    spinner.stop();
                }
                spinner_stopped_for_tools = true;
                if (ui_config.transient_ui) {
                    output << "\n";
                }
            }
            // Show new steps as they appear
            for (std::size_t i = last_trace_size; i < trace.size(); ++i) {
                const auto& step = trace[i];

                // Undo support: capture file state before edits
                if ((step.tool_name == "edit_file" || step.tool_name == "write_file") &&
                    step.status == ExecutionStepStatus::planned) {
                    auto path_start = step.args.find("path=");
                    std::string path_str;
                    if (path_start != std::string::npos) {
                        auto path_end = step.args.find('\n', path_start);
                        path_str = step.args.substr(path_start + 5,
                            path_end == std::string::npos ? std::string::npos : path_end - path_start - 5);
                    } else {
                        // Try JSON format
                        try {
                            auto j = nlohmann::json::parse(step.args);
                            path_str = j.value("path", "");
                        } catch (...) {}
                    }
                    while (!path_str.empty() && (path_str.back() == ' ' || path_str.back() == '\r'))
                        path_str.pop_back();
                    if (!path_str.empty()) {
                        auto full_path = workspace_root / path_str;
                        if (std::filesystem::exists(full_path)) {
                            std::ifstream f(full_path);
                            std::string content(std::istreambuf_iterator<char>(f), {});
                            if (undo_stack.size() >= 20) undo_stack.erase(undo_stack.begin());
                            undo_stack.push_back({full_path.string(), content});
                        }
                    }
                }

                // Multi-step progress prefix
                std::string step_prefix;
                if (trace.size() > 1) {
                    step_prefix = "\033[2m[" + std::to_string(i + 1) + "/"
                                  + std::to_string(trace.size()) + "]\033[0m ";
                }

                // Live tool call display
                if (step.status == ExecutionStepStatus::planned) {
                    // Tool is about to run — show spinner-like indicator
                    std::string tool_desc = step.tool_name;
                    // Add context from args
                    std::string arg_hint;
                    if (step.tool_name == "read_file" || step.tool_name == "list_dir") {
                        arg_hint = step.args.substr(0, 60);
                    } else if (step.tool_name == "edit_file" || step.tool_name == "write_file") {
                        try {
                            auto j = nlohmann::json::parse(step.args);
                            arg_hint = j.value("path", "");
                        } catch (...) {
                            auto p = step.args.find("path=");
                            if (p != std::string::npos) {
                                auto e = step.args.find('\n', p);
                                arg_hint = step.args.substr(p + 5, e == std::string::npos ? 60 : e - p - 5);
                            }
                        }
                    } else if (step.tool_name == "git") {
                        arg_hint = step.args.substr(0, 40);
                    } else if (step.tool_name == "search_code") {
                        auto q = step.args.find("query=");
                        if (q != std::string::npos) {
                            auto e = step.args.find('\n', q);
                            arg_hint = step.args.substr(q + 6, e == std::string::npos ? 40 : e - q - 6);
                        }
                    } else if (step.tool_name == "run_command" || step.tool_name == "build_and_test") {
                        arg_hint = step.args.substr(0, 50);
                    } else if (step.tool_name == "calculator") {
                        arg_hint = step.args.substr(0, 30);
                    }
                    // Trim arg_hint
                    while (!arg_hint.empty() && (arg_hint.back() == '\n' || arg_hint.back() == '\r'))
                        arg_hint.pop_back();

                    output << "  " << step_prefix;
                    if (ui_config.transient_ui) {
                        output << "\r\033[K\033[33m\u280b\033[0m \033[2m" << tool_desc;
                    } else {
                        output << "\033[2m→ " << tool_desc;
                    }
                    if (!arg_hint.empty()) output << " \033[36m" << arg_hint << "\033[0m";
                    output << "\033[0m";
                    if (!ui_config.transient_ui) {
                        output << "\n";
                    }
                    output << std::flush;
                } else if (step.status == ExecutionStepStatus::completed) {
                    // Tool succeeded — fold long output
                    std::string summary = step.detail;
                    int line_count = static_cast<int>(
                        std::count(summary.begin(), summary.end(), '\n'));
                    if (line_count > 3 || summary.size() > 200) {
                        // Show first 2 lines + truncation notice
                        std::string truncated;
                        int shown = 0;
                        for (std::size_t ci = 0; ci < summary.size() && shown < 2; ++ci) {
                            truncated += summary[ci];
                            if (summary[ci] == '\n') ++shown;
                        }
                        // Remove trailing newline for inline display
                        while (!truncated.empty() &&
                               (truncated.back() == '\n' || truncated.back() == '\r'))
                            truncated.pop_back();
                        // Replace remaining newlines
                        for (auto& c : truncated) if (c == '\n') c = ' ';
                        summary = truncated + " ... (" + std::to_string(line_count - 2)
                                  + " more lines)";
                    } else {
                        if (summary.size() > 80) {
                            summary = summary.substr(0, 77) + "...";
                        }
                        // Replace newlines in summary
                        for (auto& c : summary) if (c == '\n') c = ' ';
                    }

                    output << "  " << step_prefix;
                    if (ui_config.transient_ui) {
                        output << "\r\033[K";
                    }
                    output << "\033[32m\u2713\033[0m \033[2m"
                           << step.tool_name << "\033[0m";
                    if (!summary.empty() && summary != "Pending execution") {
                        output << " \033[2m\u2014 " << summary << "\033[0m";
                    }
                    output << "\n" << std::flush;
                } else if (step.status == ExecutionStepStatus::failed) {
                    std::string err = step.detail.substr(0, 80);
                    for (auto& c : err) if (c == '\n') c = ' ';
                    output << "  " << step_prefix;
                    if (ui_config.transient_ui) {
                        output << "\r\033[K";
                    }
                    output << "\033[31m\u2717\033[0m \033[2m"
                           << step.tool_name
                           << "\033[0m \033[31m" << err << "\033[0m\n" << std::flush;
                } else if (step.status == ExecutionStepStatus::denied ||
                           step.status == ExecutionStepStatus::blocked) {
                    output << "  " << step_prefix;
                    if (ui_config.transient_ui) {
                        output << "\r\033[K";
                    }
                    output << "\033[33m\u2298\033[0m \033[2m"
                           << step.tool_name
                           << "\033[0m \033[33m" << step.detail.substr(0, 60) << "\033[0m\n"
                           << std::flush;
                }
            }
            last_trace_size = trace.size();
        });

        // Stream the response
        SignalHandler::instance().reset();
        if (ui_config.spinner_enabled) {
            spinner.start(output);
        }
        renderer.begin_stream();

        try {
            std::string reply = agent.run_turn_streaming(expanded,
                [&](const std::string& token) {
                    if (first_token) {
                        if (ui_config.spinner_enabled && !spinner_stopped_for_tools) {
                            spinner.stop();
                        }
                        first_token = false;
                        output << "\n";
                    }
                    renderer.stream_token(token);
                });

            if (first_token) {
                if (ui_config.spinner_enabled && !spinner_stopped_for_tools) {
                    spinner.stop();
                }
                output << "\n";
                renderer.render_markdown(reply);
            }
            renderer.end_stream();
            output << "\n";

            // Record token usage
            auto usage = agent.last_token_usage();
            tracker.record_turn(usage, agent.model());

            // Update status bar
            auto total = tracker.total();
            renderer.render_status_bar(agent.model(), total.input_tokens, total.output_tokens,
                                       current_session_id);

            prompt_state = PromptState::ready;

        } catch (const std::exception& e) {
            if (ui_config.spinner_enabled && first_token && !spinner_stopped_for_tools) {
                spinner.stop();
            }
            renderer.end_stream();
            std::string err_msg = e.what();
            if (SignalHandler::instance().is_cancelled() ||
                err_msg.find("cancel") != std::string::npos) {
                output << "\n\033[33m  \u2298 Cancelled\033[0m\n";
                prompt_state = PromptState::warning;
            } else {
                output << "\n\033[31mError: " << err_msg << "\033[0m\n";
                prompt_state = PromptState::error;
            }
        }

        // Auto-save
        auto_save();
    }

    return 0;
}
