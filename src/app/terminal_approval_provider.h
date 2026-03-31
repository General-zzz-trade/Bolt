#ifndef APP_TERMINAL_APPROVAL_PROVIDER_H
#define APP_TERMINAL_APPROVAL_PROVIDER_H

#include <iosfwd>
#include <string>

#include "../core/interfaces/approval_provider.h"
#include "permission_rule_engine.h"

class TerminalRenderer;
class TerminalInput;

class TerminalApprovalProvider : public IApprovalProvider {
public:
    // Legacy constructor for backward compatibility (tests, non-TTY)
    TerminalApprovalProvider(std::istream& input, std::ostream& output);

    // Rich constructor with renderer and input
    TerminalApprovalProvider(TerminalRenderer& renderer, TerminalInput& term_input,
                             std::ostream& output);

    bool approve(const ApprovalRequest& request) override;

    PermissionMode mode() const;
    void set_mode(PermissionRuleScope scope, PermissionMode mode);
    void allow_tool(PermissionRuleScope scope, const std::string& tool_name);
    void deny_tool(PermissionRuleScope scope, const std::string& tool_name);
    bool remove_tool(const std::string& tool_name);
    void clear_rules(PermissionRuleScope scope);
    PermissionRuleSnapshot rules() const;

private:
    std::istream* legacy_input_ = nullptr;
    std::ostream& output_;
    TerminalRenderer* renderer_ = nullptr;
    TerminalInput* term_input_ = nullptr;
    PermissionRuleEngine rules_;

    bool approve_legacy(const ApprovalRequest& request);
    bool approve_rich(const ApprovalRequest& request);
};

#endif
