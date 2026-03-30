#ifndef AGENT_WEB_FETCH_TOOL_H
#define AGENT_WEB_FETCH_TOOL_H

#include <memory>
#include <string>

#include "../core/interfaces/http_transport.h"
#include "tool.h"

class WebFetchTool : public Tool {
public:
    explicit WebFetchTool(std::shared_ptr<IHttpTransport> transport);

    std::string name() const override;
    std::string description() const override;
    ToolSchema schema() const override;
    ToolResult run(const std::string& args) const override;
    bool is_read_only() const override { return true; }

private:
    std::shared_ptr<IHttpTransport> transport_;

    static std::string html_to_text(const std::string& html);
    static std::string extract_url(const std::string& args);
};

#endif
