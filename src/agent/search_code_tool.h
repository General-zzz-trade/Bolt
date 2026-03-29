#ifndef AGENT_SEARCH_CODE_TOOL_H
#define AGENT_SEARCH_CODE_TOOL_H

#include <filesystem>
#include <memory>

#include "../core/interfaces/file_system.h"
#include "../core/indexing/semantic_index.h"
#include "tool.h"

class SearchCodeTool : public Tool {
public:
    SearchCodeTool(std::filesystem::path workspace_root, std::shared_ptr<IFileSystem> file_system);

    std::string name() const override;
    std::string description() const override;
    ToolSchema schema() const override;
    ToolResult run(const std::string& args) const override;
    bool is_read_only() const override { return true; }

    /// Access the semantic index (built lazily on first semantic search).
    const SemanticIndex& semantic_index() const;

private:
    std::filesystem::path workspace_root_;
    std::shared_ptr<IFileSystem> file_system_;
    mutable SemanticIndex semantic_index_;
    mutable bool semantic_built_ = false;

    ToolResult run_text_search(const std::string& query) const;
    ToolResult run_symbol_search(const std::string& query) const;
    void ensure_semantic_index() const;
};

#endif
