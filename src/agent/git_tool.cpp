#include "git_tool.h"

#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "workspace_utils.h"

namespace {

const std::unordered_set<std::string>& allowed_subcommands() {
    static const std::unordered_set<std::string> cmds = {
        "status", "diff", "log", "branch", "show", "blame", "rev-parse",
    };
    return cmds;
}

std::string extract_subcommand(const std::string& input) {
    const auto space_pos = input.find(' ');
    if (space_pos == std::string::npos) {
        return input;
    }
    return input.substr(0, space_pos);
}

}  // namespace

GitTool::GitTool(std::filesystem::path workspace_root,
                 std::shared_ptr<ICommandRunner> command_runner)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      command_runner_(std::move(command_runner)) {
    if (command_runner_ == nullptr) {
        throw std::invalid_argument("GitTool requires a command runner");
    }
}

std::string GitTool::name() const {
    return "git";
}

std::string GitTool::description() const {
    return "Run read-only git commands. Supported: status, diff, log, branch, show, blame, rev-parse";
}

ToolSchema GitTool::schema() const {
    return {name(), description(), {
        {"command", "string",
         "Git command to run (e.g. 'status', 'diff HEAD~1', 'log --oneline -10')", true},
    }};
}

ToolResult GitTool::run(const std::string& args) const {
    std::string input = trim_copy(args);

    // Strip "command=" prefix if present
    if (input.rfind("command=", 0) == 0) {
        input = trim_copy(input.substr(8));
    }

    if (input.empty()) {
        return {false, "No git command specified"};
    }

    const std::string subcommand = extract_subcommand(input);
    if (allowed_subcommands().count(subcommand) == 0) {
        return {false, "Subcommand '" + subcommand + "' is not allowed. "
                       "Supported: status, diff, log, branch, show, blame, rev-parse"};
    }

    const std::string full_command = "git " + input;
    const CommandExecutionResult result =
        command_runner_->run(full_command, workspace_root_, 30000);

    if (result.timed_out) {
        return {false, "Git command timed out"};
    }

    std::string output;
    if (!result.stdout_output.empty()) {
        output = result.stdout_output;
    }
    if (!result.stderr_output.empty()) {
        if (!output.empty()) {
            output += "\n";
        }
        output += result.stderr_output;
    }

    if (output.empty()) {
        output = "(no output)";
    }

    return {result.success, output};
}
