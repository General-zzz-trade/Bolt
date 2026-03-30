#ifndef AGENT_WEB_SEARCH_TOOL_H
#define AGENT_WEB_SEARCH_TOOL_H

#include <memory>
#include <string>
#include <vector>

#include "../core/interfaces/http_transport.h"
#include "tool.h"

class WebSearchTool : public Tool {
public:
    explicit WebSearchTool(std::shared_ptr<IHttpTransport> transport);

    std::string name() const override;
    std::string description() const override;
    ToolSchema schema() const override;
    ToolResult run(const std::string& args) const override;
    bool is_read_only() const override { return true; }

private:
    std::shared_ptr<IHttpTransport> transport_;

    struct SearchResult {
        std::string title;
        std::string url;
        std::string snippet;
    };

    std::vector<SearchResult> parse_duckduckgo_html(const std::string& html) const;
    static std::string url_encode(const std::string& value);
    static std::string strip_tags(const std::string& html);
    static std::string extract_query(const std::string& args);
};

#endif
