#include "search_code_tool.h"

#include <sstream>
#include <string>

#include "workspace_utils.h"

namespace {

std::string shorten_line(const std::string& line) {
    constexpr std::size_t kMaxLength = 180;
    if (line.size() <= kMaxLength) {
        return line;
    }
    return line.substr(0, kMaxLength) + "...";
}

}  // namespace

SearchCodeTool::SearchCodeTool(std::filesystem::path workspace_root,
                               std::shared_ptr<IFileSystem> file_system)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      file_system_(std::move(file_system)) {}

std::string SearchCodeTool::name() const {
    return "search_code";
}

std::string SearchCodeTool::description() const {
    return "Search code in the workspace. Default: text search. "
           "Use mode=symbols to find classes, functions, and structs by name.";
}

ToolSchema SearchCodeTool::schema() const {
    return {name(), description(), {
        {"query", "string", "The text or symbol name to search for", true},
        {"mode", "string", "Search mode: 'text' (default) or 'symbols'", false},
    }};
}

ToolResult SearchCodeTool::run(const std::string& args) const {
    try {
        // Parse mode if provided (mode=symbols or mode=text)
        std::string query = trim_copy(args);
        std::string mode = "text";

        // Check for mode= prefix
        if (query.rfind("mode=symbols", 0) == 0 || query.rfind("mode=symbols ", 0) == 0) {
            mode = "symbols";
            query = trim_copy(query.substr(12));
        } else if (query.rfind("mode=text", 0) == 0 || query.rfind("mode=text ", 0) == 0) {
            query = trim_copy(query.substr(9));
        }
        // Also check for key=value format from structured calling
        if (query.find("query=") != std::string::npos) {
            const auto qpos = query.find("query=");
            const auto mpos = query.find("mode=");
            if (mpos != std::string::npos) {
                const auto mend = query.find('\n', mpos);
                const std::string mode_val = trim_copy(
                    query.substr(mpos + 5, mend == std::string::npos ? std::string::npos : mend - mpos - 5));
                if (mode_val == "symbols") mode = "symbols";
            }
            const auto qend = query.find('\n', qpos);
            query = trim_copy(
                query.substr(qpos + 6, qend == std::string::npos ? std::string::npos : qend - qpos - 6));
        }

        if (query.empty()) {
            return {false, "Expected search text"};
        }

        if (mode == "symbols") {
            return run_symbol_search(query);
        }
        return run_text_search(query);
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

ToolResult SearchCodeTool::run_text_search(const std::string& query) const {
    std::ostringstream output;
    output << "SEARCH: " << query << "\n";

    constexpr std::size_t kMaxMatches = 30;
    constexpr std::uintmax_t kMaxFileBytes = 1024 * 1024;
    const TextSearchResult search_result =
        file_system_->search_text(workspace_root_, query, kMaxMatches, kMaxFileBytes);
    if (!search_result.success) {
        return {false, search_result.error};
    }

    for (const auto& match : search_result.matches) {
        output << match.relative_path
               << ":" << match.line_number << ": "
               << shorten_line(match.line) << "\n";
    }

    if (search_result.matches.empty()) {
        output << "No matches found.\n";
    }
    if (search_result.truncated) {
        output << "[truncated]\n";
    }

    return {true, output.str()};
}

ToolResult SearchCodeTool::run_symbol_search(const std::string& query) const {
    ensure_semantic_index();

    const auto results = semantic_index_.search(query, 30);

    std::ostringstream output;
    output << "SYMBOLS: " << query << "\n";

    if (results.empty()) {
        output << "No symbols found matching '" << query << "'.\n";
        output << "Try text search instead (without mode=symbols).\n";
        return {true, output.str()};
    }

    for (const auto& result : results) {
        // Make path relative to workspace
        std::string rel_path = result.file_path;
        const std::string ws = workspace_root_.string();
        if (rel_path.rfind(ws, 0) == 0) {
            rel_path = rel_path.substr(ws.size());
            if (!rel_path.empty() && (rel_path[0] == '/' || rel_path[0] == '\\')) {
                rel_path = rel_path.substr(1);
            }
        }
        output << rel_path << ":" << result.line_number
               << " [" << static_cast<int>(result.relevance * 100) << "%] "
               << result.line_content << "\n";
    }

    return {true, output.str()};
}

void SearchCodeTool::ensure_semantic_index() const {
    if (!semantic_built_) {
        semantic_index_.build(workspace_root_);
        semantic_built_ = true;
    }
}

const SemanticIndex& SearchCodeTool::semantic_index() const {
    ensure_semantic_index();
    return semantic_index_;
}
