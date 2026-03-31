#include "terminal_approval_provider.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#include "terminal_renderer.h"
#include "terminal_input.h"

namespace {

std::string trim_copy(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

}  // namespace

TerminalApprovalProvider::TerminalApprovalProvider(std::istream& input, std::ostream& output)
    : legacy_input_(&input), output_(output) {
    rules_.load();
}

TerminalApprovalProvider::TerminalApprovalProvider(TerminalRenderer& renderer,
                                                     TerminalInput& term_input,
                                                     std::ostream& output)
    : output_(output), renderer_(&renderer), term_input_(&term_input) {
    rules_.load();
}

bool TerminalApprovalProvider::approve(const ApprovalRequest& request) {
    if (rules_.is_allowed(request.tool_name)) {
        return true;
    }
    if (rules_.is_denied(request.tool_name)) {
        return false;
    }
    if (rules_.mode() == PermissionMode::auto_approve) {
        return true;
    }
    if (rules_.mode() == PermissionMode::auto_deny) {
        return false;
    }

    if (renderer_ && term_input_) {
        return approve_rich(request);
    }
    return approve_legacy(request);
}

bool TerminalApprovalProvider::approve_legacy(const ApprovalRequest& request) {
    output_ << "\n[approval]\n";
    output_ << "tool: " << request.tool_name << "\n";
    output_ << "risk: " << (request.risk.empty() ? "unspecified" : request.risk) << "\n";
    if (!request.reason.empty()) output_ << "reason: " << request.reason << "\n";
    if (!request.preview_summary.empty()) output_ << "summary: " << request.preview_summary << "\n";
    if (!request.preview_details.empty()) {
        output_ << "preview:\n" << request.preview_details << "\n";
    } else if (!request.args.empty()) {
        constexpr std::size_t kMax = 240;
        output_ << "args:\n" << (request.args.size() <= kMax ? request.args : request.args.substr(0, kMax) + "... [truncated]") << "\n";
    }
    output_ << "Approve? [y/N]: ";
    output_.flush();

    std::string response;
    if (!legacy_input_ || !std::getline(*legacy_input_, response)) {
        output_ << "\n";
        return false;
    }

    const std::string normalized = to_lower_copy(trim_copy(response));
    if (normalized == "a" || normalized == "always") {
        rules_.allow_tool(PermissionRuleScope::workspace, request.tool_name);
        return true;
    }
    if (normalized == "x" || normalized == "never" || normalized == "deny") {
        rules_.deny_tool(PermissionRuleScope::workspace, request.tool_name);
        return false;
    }
    return normalized == "y" || normalized == "yes";
}

bool TerminalApprovalProvider::approve_rich(const ApprovalRequest& request) {
    // Render the approval card
    renderer_->render_approval_card(
        request.tool_name, request.risk, request.reason,
        request.preview_summary,
        request.preview_details.empty() ? "" : request.preview_details.substr(0, 500));

    while (true) {
        output_ << "  \033[1m[y]\033[0mes  \033[1m[n]\033[0mo  \033[1m[a]\033[0mlways"
                << "  \033[1m[x]\033[0mdeny  \033[1m[d]\033[0metails > " << std::flush;

        char key = term_input_->read_single_key();
        output_ << key << "\n";

        switch (std::tolower(key)) {
            case 'y':
                return true;
            case 'a':
                rules_.allow_tool(PermissionRuleScope::workspace, request.tool_name);
                output_ << "\033[2m  (allowing " << request.tool_name
                        << " for this workspace)\033[0m\n";
                return true;
            case 'x':
                rules_.deny_tool(PermissionRuleScope::workspace, request.tool_name);
                output_ << "\033[2m  (denying " << request.tool_name
                        << " for this workspace)\033[0m\n";
                return false;
            case 'd': {
                // Show full details
                output_ << "\n\033[2m--- Full Details ---\033[0m\n";
                if (!request.preview_details.empty()) {
                    renderer_->render_markdown("```\n" + request.preview_details + "\n```");
                } else if (!request.args.empty()) {
                    renderer_->render_markdown("```\n" + request.args + "\n```");
                } else {
                    output_ << "\033[2m  (no additional details)\033[0m\n";
                }
                output_ << "\n";
                continue;  // Re-prompt
            }
            case 'n':
            case '\r':
            case '\n':
            case '\0':
            default:
                return false;
        }
    }
}

PermissionMode TerminalApprovalProvider::mode() const {
    return rules_.mode();
}

void TerminalApprovalProvider::set_mode(PermissionRuleScope scope, PermissionMode mode) {
    rules_.set_mode(scope, mode);
}

void TerminalApprovalProvider::allow_tool(PermissionRuleScope scope, const std::string& tool_name) {
    rules_.allow_tool(scope, tool_name);
}

void TerminalApprovalProvider::deny_tool(PermissionRuleScope scope, const std::string& tool_name) {
    rules_.deny_tool(scope, tool_name);
}

bool TerminalApprovalProvider::remove_tool(const std::string& tool_name) {
    return rules_.remove_tool(tool_name);
}

void TerminalApprovalProvider::clear_rules(PermissionRuleScope scope) {
    rules_.clear(scope);
}

PermissionRuleSnapshot TerminalApprovalProvider::rules() const {
    return rules_.snapshot();
}
