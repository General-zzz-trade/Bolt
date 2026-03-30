#ifndef AGENT_BROWSER_TOOL_H
#define AGENT_BROWSER_TOOL_H

#include <filesystem>
#include <memory>
#include <string>

#include "../core/interfaces/command_runner.h"
#include "tool.h"

class BrowserTool : public Tool {
public:
    BrowserTool(std::filesystem::path workspace_root,
                std::shared_ptr<ICommandRunner> command_runner);

    std::string name() const override;
    std::string description() const override;
    ToolResult run(const std::string& args) const override;
    ToolSchema schema() const override;
    bool is_read_only() const override { return true; }

private:
    std::filesystem::path workspace_root_;
    std::shared_ptr<ICommandRunner> command_runner_;

    static std::string find_chrome_binary();
    ToolResult navigate(const std::string& url) const;
    ToolResult screenshot(const std::string& url, const std::string& output_path) const;
    ToolResult extract_text(const std::string& url) const;
    static std::string html_to_text(const std::string& html);
};

#endif
