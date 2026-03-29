#ifndef CORE_INDEXING_SEMANTIC_INDEX_H
#define CORE_INDEXING_SEMANTIC_INDEX_H

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "file_index.h"

/// Semantic code index that extracts symbols (classes, functions, structs)
/// and provides relevance-ranked search results.
/// Builds on top of FileIndex trigram search with symbol-level ranking.
class SemanticIndex {
public:
    struct SymbolInfo {
        std::string name;
        std::string kind;  // "class", "function", "struct", "method"
        std::string file_path;
        std::size_t line_number;
    };

    struct SearchResult {
        std::string file_path;
        std::size_t line_number;
        std::string line_content;
        double relevance;  // 0.0 to 1.0
    };

    /// Build semantic index over all source files under root_dir.
    void build(const std::filesystem::path& root_dir,
               const std::vector<std::string>& extensions = {
                   ".cpp", ".h", ".hpp", ".c", ".py", ".js", ".ts",
                   ".rs", ".go", ".java"});

    /// Search with semantic ranking. Results sorted by relevance.
    std::vector<SearchResult> search(const std::string& query,
                                      std::size_t max_results = 50) const;

    /// Get all symbols in a file.
    std::vector<SymbolInfo> symbols_in_file(const std::string& file_path) const;

    /// Get file-level summary (list of symbols).
    std::string file_summary(const std::string& file_path) const;

    /// Number of indexed symbols.
    std::size_t symbol_count() const;

    bool is_ready() const { return ready_; }

private:
    struct FileSummary {
        std::string path;
        std::vector<SymbolInfo> symbols;
        std::string summary_text;  // "class Foo, function bar, struct Baz"
    };

    std::unordered_map<std::string, FileSummary> file_summaries_;
    std::vector<SymbolInfo> all_symbols_;
    bool ready_ = false;

    void extract_symbols(const std::filesystem::path& file_path);
    static std::vector<SymbolInfo> extract_cpp_symbols(
        const std::string& content, const std::string& path);
    static std::vector<SymbolInfo> extract_python_symbols(
        const std::string& content, const std::string& path);
    static std::vector<SymbolInfo> extract_js_symbols(
        const std::string& content, const std::string& path);
    static double compute_relevance(const std::string& query,
                                     const std::string& symbol_name,
                                     const std::string& file_summary);
};

#endif
