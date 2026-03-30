#ifndef AGENT_EMAIL_TOOL_H
#define AGENT_EMAIL_TOOL_H

#include <memory>
#include <string>

#include "tool.h"
#include "../core/interfaces/http_transport.h"

class EmailTool : public Tool {
public:
    explicit EmailTool(std::shared_ptr<IHttpTransport> transport);

    std::string name() const override;
    std::string description() const override;
    ToolSchema schema() const override;
    ToolResult run(const std::string& args) const override;

private:
    std::shared_ptr<IHttpTransport> transport_;
};

#endif
