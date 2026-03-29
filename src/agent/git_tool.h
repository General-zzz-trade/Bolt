#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "../core/interfaces/command_runner.h"
#include "tool.h"

class GitTool : public Tool {
public:
    GitTool(std::filesystem::path workspace_root,
            std::shared_ptr<ICommandRunner> command_runner);

    std::string name() const override;
    std::string description() const override;
    ToolResult run(const std::string& args) const override;
    ToolSchema schema() const override;
    bool is_read_only() const override { return true; }

private:
    std::filesystem::path workspace_root_;
    std::shared_ptr<ICommandRunner> command_runner_;
};
