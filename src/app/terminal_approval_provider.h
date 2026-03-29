#ifndef APP_TERMINAL_APPROVAL_PROVIDER_H
#define APP_TERMINAL_APPROVAL_PROVIDER_H

#include <iosfwd>
#include <set>
#include <string>

#include "../core/interfaces/approval_provider.h"

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

private:
    std::istream* legacy_input_ = nullptr;
    std::ostream& output_;
    TerminalRenderer* renderer_ = nullptr;
    TerminalInput* term_input_ = nullptr;
    std::set<std::string> always_allow_;

    bool approve_legacy(const ApprovalRequest& request);
    bool approve_rich(const ApprovalRequest& request);
};

#endif
