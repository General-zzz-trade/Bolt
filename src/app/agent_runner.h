#ifndef APP_AGENT_RUNNER_H
#define APP_AGENT_RUNNER_H

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>

class Agent;
class IApprovalProvider;

std::string build_agent_banner(const Agent& agent);
int run_agent_single_turn(Agent& agent, const std::string& prompt, std::ostream& output);
int run_agent_interactive_loop(Agent& agent, std::istream& input, std::ostream& output,
                               const std::filesystem::path& workspace_root = {},
                               bool resume_last = false,
                               std::shared_ptr<IApprovalProvider> approval_provider = nullptr);

#endif
